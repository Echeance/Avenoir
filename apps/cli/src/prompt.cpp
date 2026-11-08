#include "prompt.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "avenoir/terminal/style.hpp"

#include "console.hpp"

namespace avenoir::cli {

namespace {

/// Touches simulées au redimensionnement : U+10FFFE et U+10FFFF,
/// non-caractères Unicode, jamais produits par le clavier.
constexpr char32_t kNotifyKey = 0x10FFFD; ///< Messages de `poll` à montrer.
constexpr char32_t kResizeKey = 0x10FFFE;
constexpr char32_t kRestoreKey = 0x10FFFF;

/// Nombre de caractères Unicode d'un texte UTF-8 (unité des positions de replxx).
int code_points(std::string_view text) {
    int count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

/// Les `count` premiers caractères Unicode d'un texte UTF-8.
std::string utf8_prefix(std::string_view text, int count) {
    std::size_t end = 0;
    int seen = 0;
    while (end < text.size()) {
        if ((static_cast<unsigned char>(text[end]) & 0xC0U) != 0x80U) {
            if (seen == count) {
                break;
            }
            ++seen;
        }
        ++end;
    }
    return std::string(text.substr(0, end));
}

std::string trim_lower(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return out;
}

} // namespace

Prompt::Prompt(std::vector<std::string> commands, std::optional<std::filesystem::path> history_file)
    : commands_(std::move(commands)), history_file_(std::move(history_file)) {
    replxx_.install_window_change_handler();
    // Les couleurs de replxx restent actives même avec NO_COLOR : sans elles,
    // replxx n'affiche plus les suggestions, qui portent la bordure du bas.
    // Avec NO_COLOR, la suggestion n'est pas colorée (voir plus bas).
    replxx_.set_max_history_size(1000);
    replxx_.set_word_break_characters(" \t");
    replxx_.set_completion_callback(
        [this](const std::string& input, int& context_length) -> replxx::Replxx::completions_t {
            // Complète le dernier mot s'il commence par « / ».
            const auto start = input.find_last_of(' ');
            const std::string word = start == std::string::npos ? input : input.substr(start + 1);
            replxx::Replxx::completions_t completions;
            if (!word.starts_with('/')) {
                return completions;
            }
            context_length = static_cast<int>(word.size());
            for (const auto& command : commands_) {
                if (command.starts_with(word)) {
                    completions.emplace_back(command);
                }
            }
            return completions;
        });
    // Bordure du bas de la barre de saisie : affichée comme une « suggestion »
    // (hint) de replxx, seul contenu qu'il réécrit sous la saisie à chaque
    // frappe (le reste de l'écran sous le curseur est effacé). Le contexte
    // couvre toute la saisie : seule la partie « \n─── » est affichée.
    replxx_.set_hint_callback([this](const std::string& input, int& context_length,
                                     replxx::Replxx::Color& color) -> replxx::Replxx::hints_t {
        if (!hooks_.border) {
            return {};
        }
        context_length = code_points(input);
        color = terminal::enabled() ? replxx::Replxx::Color::GRAY : replxx::Replxx::Color::DEFAULT;
#ifdef _WIN32
        // La console Windows ne revient pas en début de ligne sur « \n » seul.
        return {input + "\r\n" + hooks_.border()};
#else
        return {input + "\n" + hooks_.border()};
#endif
    });
    // Chaque modification du texte : la barre s'agrandit si besoin.
    replxx_.set_modify_callback([this](std::string& line, int& /*cursor*/) {
        if (hooks_.changed) {
            hooks_.changed(current_prompt_, line);
        }
    });
    // Touche simulée par le thread de surveillance quand le terminal change
    // de taille : la barre est recalée, puis replxx redessine l'invite et la
    // saisie depuis la position du curseur.
    //
    // replxx garde la bordure du bas (sa suggestion) en cache tant que le
    // texte ne change pas : elle garderait l'ancienne largeur. On ajoute donc
    // un instant une espace en fin de texte (invisible), puis une seconde
    // touche simulée rétablit le texte : la bordure est recalculée à la
    // nouvelle largeur. Sous Windows, la saisie est relancée (voir plus bas).
    replxx_.bind_key(kResizeKey, [this](char32_t /*code*/) {
        if (restoring_) {
            // Redimensionnement arrivé avant le rétablissement du précédent
            // (le texte est encore celui, allongé d'une espace, du recalage) :
            // traité après lui, dans l'ordre de la file des touches.
            replxx_.emulate_key_press(kResizeKey);
            return replxx::Replxx::ACTION_RESULT::CONTINUE;
        }
#ifdef _WIN32
        if (restart_) {
            return replxx::Replxx::ACTION_RESULT::CONTINUE; // Relance déjà prévue.
        }
#endif
        const auto state = replxx_.get_state();
        pending_text_ = state.text();
        pending_cursor_ = state.cursor_position();
#ifdef _WIN32
        // Sous Windows, replxx ne voit pas les redimensionnements de la
        // console : il garderait l'ancienne largeur et placerait mal la saisie
        // (dédoublée sur la bordure, décalée d'une ligne). La saisie est donc
        // relancée : Entrée simulée, puis read_line rouvre une saisie neuve,
        // à la nouvelle largeur, avec le même texte et le même curseur.
        restart_ = true;
        replxx_.emulate_key_press(replxx::Replxx::KEY::ENTER);
        return replxx::Replxx::ACTION_RESULT::CONTINUE;
#else
        restoring_ = true;
        if (hooks_.resized) {
            const std::string before = utf8_prefix(
                pending_text_, pending_cursor_ < 0 ? code_points(pending_text_) : pending_cursor_);
            hooks_.resized(current_prompt_, terminal::display_width(before));
        }
        const std::string padded = pending_text_ + " ";
        replxx_.set_state(replxx::Replxx::State(padded.c_str(), code_points(padded)));
        replxx_.emulate_key_press(kRestoreKey);
        return replxx::Replxx::ACTION_RESULT::CONTINUE; // replxx redessine ensuite
#endif
    });
    // Messages de surveillance : montrés dans le thread de replxx, entre deux
    // affichages de la saisie, pour ne jamais croiser sa frappe.
    replxx_.bind_key(kNotifyKey, [this](char32_t /*code*/) {
        std::vector<std::string> notes;
        {
            const std::scoped_lock lock(notes_mutex_);
            notes.swap(notes_);
        }
        if (hooks_.notify) {
            for (const auto& note : notes) {
                hooks_.notify(note);
            }
        }
        return replxx::Replxx::ACTION_RESULT::CONTINUE;
    });
    replxx_.bind_key(kRestoreKey, [this](char32_t /*code*/) {
        replxx_.set_state(replxx::Replxx::State(pending_text_.c_str(), pending_cursor_));
        restoring_ = false;
        return replxx::Replxx::ACTION_RESULT::CONTINUE;
    });
    if (history_file_) {
        replxx_.history_load(history_file_->string());
    }
}

void Prompt::set_hooks(Hooks hooks) {
    hooks_ = std::move(hooks);
}

Prompt::Input Prompt::read(const std::string& prompt, bool remember) {
    if (hooks_.before) {
        hooks_.before();
    }
    current_prompt_ = prompt;
    Input input;
    {
        // Surveillance de la taille du terminal pendant la saisie seulement.
        std::jthread watcher;
        if (hooks_.resized) {
            watcher = std::jthread([this](const std::stop_token& stop) { watch_size(stop); });
        }
        std::jthread events;
        if (hooks_.poll && hooks_.notify) {
            events = std::jthread([this](const std::stop_token& stop) { watch_events(stop); });
        }
        input = read_line(prompt, remember);
    } // arrêt et attente du thread de surveillance
    if (hooks_.after) {
        hooks_.after(prompt, input);
    }
    return input;
}

void Prompt::watch_size(const std::stop_token& stop) {
    using namespace std::chrono_literals;
    std::size_t rows = terminal::terminal_height();
    std::size_t columns = terminal::terminal_width();
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    bool pending = false; // Taille changée, recalage à faire une fois stable.
    while (!stop.stop_requested()) {
        // Réveil immédiat à l'arrêt, sinon vérification toutes les 100 ms.
        (void)wake.wait_for(lock, stop, 100ms, [] { return false; });
        const std::size_t new_rows = terminal::terminal_height();
        const std::size_t new_columns = terminal::terminal_width();
        if (new_rows != rows || new_columns != columns) {
            // Une fenêtre agrandie ou ancrée passe par plusieurs tailles :
            // on attend qu'elle soit stable avant de recaler la barre.
            rows = new_rows;
            columns = new_columns;
            pending = true;
            continue;
        }
        if (pending) {
            pending = false;
            replxx_.emulate_key_press(kResizeKey); // sûr depuis un autre thread
        }
    }
}

void Prompt::watch_events(const std::stop_token& stop) {
    using namespace std::chrono_literals;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    while (!stop.stop_requested()) {
        // Réveil immédiat à l'arrêt (fin de la saisie), sinon toutes les 3 s.
        if (wake.wait_for(lock, stop, 3s, [] { return false; }) || stop.stop_requested()) {
            break;
        }
        auto messages = hooks_.poll();
        if (messages.empty()) {
            continue;
        }
        {
            const std::scoped_lock notes_lock(notes_mutex_);
            std::ranges::move(messages, std::back_inserter(notes_));
        }
        replxx_.emulate_key_press(kNotifyKey); // sûr depuis un autre thread
    }
}

void Prompt::retype(const std::string& text, int cursor) {
    // Le texte est retapé touche par touche : replxx l'affiche comme une
    // frappe ordinaire (la barre grandit au fil des lignes), ce qui place la
    // saisie sans erreur ; un texte préchargé s'afficherait décalé.
    int count = 0;
    char32_t code = 0;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte & 0xC0U) == 0x80U) {
            code = (code << 6U) | (byte & 0x3FU); // octet de continuation
            continue;
        }
        if (count > 0) {
            replxx_.emulate_key_press(code);
        }
        // Bits utiles de l'octet de tête : 7 (ASCII), 5, 4 ou 3.
        if (byte < 0x80U) {
            code = byte;
        } else if (byte < 0xE0U) {
            code = byte & 0x1FU;
        } else if (byte < 0xF0U) {
            code = byte & 0x0FU;
        } else {
            code = byte & 0x07U;
        }
        ++count;
    }
    if (count > 0) {
        replxx_.emulate_key_press(code);
    }
    for (int left = cursor < 0 ? 0 : count - cursor; left > 0; --left) {
        replxx_.emulate_key_press(replxx::Replxx::KEY::LEFT);
    }
}

Prompt::Input Prompt::read_line(const std::string& prompt, bool remember) {
    errno = 0;
    const char* line = replxx_.input(prompt);
    // Saisie interrompue par un redimensionnement (Windows) : la barre est
    // recalée, puis une saisie neuve reprend le texte et la position du curseur.
    while (line != nullptr && restart_) {
        restart_ = false;
        if (hooks_.before) {
            hooks_.before();
        }
        retype(pending_text_, pending_cursor_);
        errno = 0;
        line = replxx_.input(prompt);
    }
    if (line == nullptr) {
        // replxx signale Ctrl+C par errno = EAGAIN, la fin de l'entrée sinon.
        return {.status = errno == EAGAIN ? Status::Interrupted : Status::EndOfInput, .text = {}};
    }
    std::string text(line);
    if (remember && !text.empty()) {
        replxx_.history_add(text);
        if (history_file_) {
            replxx_.history_save(history_file_->string());
        }
    }
    return {.status = Status::Line, .text = std::move(text)};
}

bool Prompt::ask_yes_no(const std::string& question) {
    const Input answer = read(question, /*remember=*/false);
    if (answer.status == Status::Interrupted) {
        raise_interrupt();
        return false;
    }
    if (answer.status != Status::Line) {
        return false;
    }
    const std::string text = trim_lower(answer.text);
    return text == "o" || text == "oui" || text == "y" || text == "yes";
}

} // namespace avenoir::cli
