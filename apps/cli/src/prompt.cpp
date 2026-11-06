#include "prompt.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "console.hpp"

namespace avenoir::cli {

namespace {

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
    if (history_file_) {
        replxx_.history_load(history_file_->string());
    }
}

Prompt::Input Prompt::read(const std::string& prompt, bool remember) {
    errno = 0;
    const char* line = replxx_.input(prompt);
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
