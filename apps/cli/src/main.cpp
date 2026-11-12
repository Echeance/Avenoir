// Interface en ligne de commande d'Avenoir : un client du démon (`avenoird`),
// qu'elle lance s'il ne répond pas (docs/PROJECT.md § 2.1, décision 53).
//
// Usage : avenoir [chemin/vers/config.toml]
//   Sans argument : le démon de l'utilisateur et sa configuration scellée
//   (~/.config/avenoir/config.toml, %APPDATA%\avenoir\config.toml), créée au
//   premier lancement par la création guidée.
//   Avec un chemin : un démon privé pour cette configuration (mode
//   développement, non scellée), arrêté quand la CLI part.
// Logs  : variable AVENOIR_LOG ; ceux du démon sont dans logs/daemon.log.
// Couleurs : désactivées hors d'un terminal ou si NO_COLOR est défini.
//
// Ctrl+C ne ferme pas Avenoir : il arrête l'action en cours puis propose
// d'annuler ce qui peut l'être (docs/PROJECT.md § 2.4). /quitter ou Ctrl+D
// quitte la CLI ; le démon continue (/arreter l'arrête).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/env.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"
#include "avenoir/ipc/peer.hpp"
#include "avenoir/ipc/transport.hpp"
#include "avenoir/storage/store.hpp"
#include "avenoir/terminal/style.hpp"

#include "console.hpp"
#include "display.hpp"
#include "launcher.hpp"
#include "prompt.hpp"

namespace {

using namespace avenoir;
using cli::display;
using nlohmann::json;
using terminal::Color;
using terminal::paint;
using namespace std::chrono_literals;

/// Délai pour qu'un démon lancé par la CLI réponde.
constexpr auto kDaemonStartup = 20s;

void report_fatal(const char* message) noexcept {
    (void)std::fputs("Erreur : ", stderr);
    (void)std::fputs(message, stderr);
    (void)std::fputs("\n", stderr);
}

/// Logs de la CLI sur stderr (stdout reste au dialogue).
void setup_logging() {
    const auto logger = spdlog::stderr_color_mt("avenoir");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%^[%n %l]%$ %v");
    spdlog::set_level(spdlog::level::warn);
    if (const auto level = core::read_env("AVENOIR_LOG")) {
        spdlog::set_level(spdlog::level::from_str(*level));
    }
}

/// Texte mis en forme par le démon : sans couleurs si ce terminal n'en veut pas.
std::string shown(std::string_view text) {
    return terminal::enabled() ? std::string(text) : terminal::strip_styles(text);
}

std::string text_of(const json& params) {
    return params.contains("text") && params["text"].is_string() ? params["text"].get<std::string>()
                                                                 : std::string{};
}

core::ToolCall call_from(const json& object) {
    core::ToolCall call;
    if (object.is_object()) {
        call.id = object.value("id", "");
        call.name = object.value("name", "");
        if (object.contains("arguments")) {
            call.arguments = object["arguments"];
        }
    }
    return call;
}

/// Adresse du démon : celle de l'utilisateur, ou un démon privé pour une
/// configuration donnée en argument (adresse propre à son chemin).
core::Result<ipc::Endpoint> daemon_endpoint(const std::optional<std::filesystem::path>& config) {
    auto base = ipc::default_endpoint();
    if (!base || !config) {
        return base;
    }
    std::error_code error;
    const auto absolute =
        std::filesystem::weakly_canonical(std::filesystem::absolute(*config), error);
    const auto hash = std::hash<std::string>{}(absolute.generic_string());
    return ipc::endpoint_variant(*base, std::format("dev-{:016x}", hash));
}

/// Se connecte au démon ; le lance s'il ne répond pas, puis attend qu'il soit prêt.
core::Result<std::unique_ptr<ipc::Connection>>
connect_daemon(const ipc::Endpoint& endpoint, const std::optional<std::filesystem::path>& config) {
    if (auto connection = ipc::connect(endpoint)) {
        return connection;
    }
    std::vector<std::string> arguments{"--endpoint", endpoint.address};
    if (config) {
        std::error_code error;
        arguments.emplace_back("--config");
        arguments.push_back(std::filesystem::absolute(*config, error).string());
        arguments.emplace_back("--exit-when-idle");
    }
    if (auto started = cli::start_daemon(arguments); !started) {
        return std::unexpected(std::move(started.error()));
    }
    const auto deadline = std::chrono::steady_clock::now() + kDaemonStartup;
    while (true) {
        auto connection = ipc::connect(endpoint);
        if (connection || std::chrono::steady_clock::now() > deadline) {
            if (!connection) {
                return core::make_error(core::ErrorCode::Ipc,
                                        "le démon ne répond pas (voir logs/daemon.log du dossier "
                                        "de données)");
            }
            return connection;
        }
        std::this_thread::sleep_for(100ms);
    }
}

/// Ce que le démon demande à cette CLI : afficher, questionner, confirmer.
class Client {
  public:
    explicit Client(ipc::Peer& peer) : peer_(&peer) {}

    void attach(cli::Prompt& prompt) { prompt_ = &prompt; }

    void install() {
        peer_->on_notification("ui", [this](const json& params) { show(params); });
        peer_->on_request("ask", [this](const json& params, const std::stop_token&) {
            const auto answer = ask(text_of_question(params));
            return core::Result<json>(json{{"answer", answer ? json(*answer) : json(nullptr)}});
        });
        peer_->on_request("ask_yes_no", [this](const json& params, const std::stop_token&) {
            return core::Result<json>(ask_yes_no(text_of_question(params)));
        });
        peer_->on_request("confirm", [this](const json& params, const std::stop_token&) {
            return core::Result<json>(confirm(params));
        });
        peer_->on_closed([this] { lost_ = true; });
    }

    /// Messages discrets reçus (modèle déchargé par Ollama…) : montrés pendant
    /// la saisie par la barre, ou entre deux requêtes.
    [[nodiscard]] std::vector<std::string> take_notes() {
        const std::scoped_lock lock(notes_mutex_);
        std::vector<std::string> notes(notes_.begin(), notes_.end());
        notes_.clear();
        return notes;
    }

    [[nodiscard]] bool lost() const noexcept { return lost_; }

    /// Appel au démon pendant lequel la toupie tourne ; Ctrl+C l'interrompt
    /// (le démon termine proprement et peut encore proposer d'annuler).
    core::Result<json> run(std::string_view method, json params) {
        (void)cli::take_interrupt();
        std::optional<core::Result<json>> result;
        std::atomic<bool> done{false};
        {
            std::jthread worker([this, method, &params, &result,
                                 &done](const std::stop_token& stop) {
                result = peer_->call(method, std::move(params), stop, ipc::Peer::OnStop::AwaitEnd);
                done = true;
            });
            bool stop_announced = false;
            while (!done) {
                if (cli::take_interrupt() && !stop_announced) {
                    worker.request_stop();
                    display().warning("Arrêt demandé…");
                    stop_announced = true;
                }
                for (const auto& note : take_notes()) {
                    display().notify(note);
                }
                display().tick();
                std::this_thread::sleep_for(50ms);
            }
        }
        if (!result) {
            return core::make_error(core::ErrorCode::Ipc, "appel sans réponse");
        }
        return std::move(*result);
    }

  private:
    static std::string text_of_question(const json& params) {
        return params.contains("question") && params["question"].is_string()
                   ? params["question"].get<std::string>()
                   : std::string{};
    }

    void show(const json& params) {
        const std::string op = params.value("op", "");
        const std::string text = shown(text_of(params));
        if (op == "info") {
            display().info(text);
        } else if (op == "notify") {
            const std::scoped_lock lock(notes_mutex_);
            notes_.push_back(text);
        } else if (op == "success") {
            display().success(text);
        } else if (op == "warning") {
            display().warning(text);
        } else if (op == "error") {
            display().error(text);
        } else if (op == "plain") {
            display().plain(text);
        } else if (op == "user") {
            display().user(text);
        } else if (op == "assistant") {
            display().assistant(text);
        } else if (op == "tool_calls") {
            std::vector<core::ToolCall> calls;
            for (const auto& call : params.value("calls", json::array())) {
                calls.push_back(call_from(call));
            }
            display().tool_calls(calls);
        } else if (op == "tool_result") {
            if (auto message =
                    storage::detail::message_from_json(params.value("message", json::object()))) {
                if (const auto* result = std::get_if<core::ToolResultMessage>(&*message)) {
                    display().tool_result(*result);
                }
            }
        } else if (op == "messages") {
            std::vector<core::Message> messages;
            for (const auto& item : params.value("messages", json::array())) {
                if (auto message = storage::detail::message_from_json(item)) {
                    messages.push_back(std::move(*message));
                }
            }
            std::optional<std::size_t> limit;
            if (params.contains("limit") && params["limit"].is_number_unsigned()) {
                limit = params["limit"].get<std::size_t>();
            }
            cli::print_messages(messages, limit);
        } else if (op == "clear") {
            display().clear();
        } else if (op == "banner") {
            display().banner(shown(params.value("llm", "")), params.value("tools", std::size_t{0}),
                             shown(params.value("conversation", "")));
        } else if (op == "start_thinking") {
            display().start_thinking();
        } else if (op == "activity") {
            display().set_activity(text);
        } else if (op == "stop_thinking") {
            display().stop_thinking();
        } else if (op == "stream_text") {
            display().stream_text(text_of(params));
        } else if (op == "stream_end") {
            display().stream_end(params.value("kept", true));
        }
    }

    std::optional<std::string> ask(const std::string& question) {
        display().suspend_thinking();
        std::optional<std::string> answer;
        if (prompt_ != nullptr) {
            auto input = prompt_->read(shown(question) + " ", /*remember=*/false);
            if (input.status == cli::Prompt::Status::Line) {
                answer = std::move(input.text);
            } else {
                (void)cli::take_interrupt(); // abandon de la question
            }
        }
        display().resume_thinking();
        return answer;
    }

    bool ask_yes_no(const std::string& question) {
        display().suspend_thinking();
        const bool yes = prompt_ != nullptr && prompt_->ask_yes_no(shown(question) + " ");
        display().resume_thinking();
        return yes;
    }

    bool confirm(const json& params) {
        const json spec_json = params.value("spec", json::object());
        core::ToolSpec spec;
        spec.name = spec_json.value("name", "");
        spec.description = spec_json.value("description", "");
        spec.risk = core::risk_level_from_string(spec_json.value("risk", "sensitive"))
                        .value_or(core::RiskLevel::Sensitive);
        const core::ToolCall call = call_from(params.value("call", json::object()));
        display().suspend_thinking();
        display().confirmation(spec, call);
        const bool approved =
            prompt_ != nullptr &&
            prompt_->ask_yes_no(
                "  " + paint("Autoriser ?", {.color = Color::Yellow, .bold = true}) + " [o/N] ");
        display().resume_thinking();
        return approved;
    }

    ipc::Peer* peer_;
    cli::Prompt* prompt_ = nullptr;
    std::mutex notes_mutex_;
    std::deque<std::string> notes_;
    std::atomic<bool> lost_{false};
};

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

int run(int argc, char** argv) {
    setup_logging();
    cli::install_console();
    terminal::set_enabled(terminal::detect_color_support());

    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    std::optional<std::filesystem::path> config;
    if (args.size() > 1) {
        config = std::filesystem::path(args[1]);
    }
    auto endpoint = daemon_endpoint(config);
    if (!endpoint) {
        display().error(core::describe(endpoint.error()));
        return EXIT_FAILURE;
    }
    auto connection = connect_daemon(*endpoint, config);
    if (!connection) {
        display().error(core::describe(connection.error()));
        return EXIT_FAILURE;
    }
    ipc::Peer peer(std::move(*connection));
    Client client(peer);
    client.install();

    // Barre de saisie : historique des saisies dans le dossier de données.
    std::optional<std::filesystem::path> history_file;
    if (const auto data = storage::data_directory()) {
        history_file = *data / "cli_history.txt";
    }
    cli::Prompt prompt({}, history_file);
    prompt.set_hooks(cli::Prompt::Hooks{
        .before = [] { display().prepare_input(); },
        .after =
            [](const std::string& marker, const cli::Prompt::Input& input) {
                display().finish_input(marker, input.status == cli::Prompt::Status::Line,
                                       input.text);
            },
        .changed = [](const std::string& marker,
                      const std::string& line) { display().fit_input(marker, line); },
        .resized =
            [](const std::string& marker, std::size_t before_cursor) {
                display().redraw_for_resize(marker, before_cursor);
            },
        .border = [] { return cli::Display::bar_rule(); },
        .poll = [&client] { return client.take_notes(); },
        .notify = [](const std::string& message) { display().notify(message); },
    });
    client.attach(prompt);
    peer.start();

    // Terminal rendu dans son état normal à la sortie, même en cas d'erreur.
    display().enable_pinned_bar(terminal::detect_vt_support());
    struct ScreenGuard {
        ScreenGuard() = default;
        ScreenGuard(const ScreenGuard&) = delete;
        ScreenGuard& operator=(const ScreenGuard&) = delete;
        ScreenGuard(ScreenGuard&&) = delete;
        ScreenGuard& operator=(ScreenGuard&&) = delete;
        ~ScreenGuard() { display().shutdown(); }
    } const screen_guard;

    // Accueil (et, au premier lancement, création guidée des réglages).
    auto opened = client.run("session/open", {{"interactive", cli::stdin_is_terminal()}});
    if (!opened) {
        display().error(core::describe(opened.error()));
        return EXIT_FAILURE;
    }
    std::vector<std::string> completions;
    for (const auto& name : opened->value("completions", json::array())) {
        if (name.is_string()) {
            completions.push_back(name.get<std::string>());
        }
    }
    prompt.set_commands(std::move(completions));
    std::string conversation = opened->value("conversation", "");

    while (!client.lost()) {
        for (const auto& note : client.take_notes()) {
            display().notify(note);
        }
        const auto input = prompt.read(cli::Display::prompt_marker(), /*remember=*/true);
        if (input.status == cli::Prompt::Status::EndOfInput) {
            if (auto described = client.run("session/describe", json::object())) {
                conversation = described->value("conversation", conversation);
            }
            break;
        }
        if (input.status == cli::Prompt::Status::Interrupted) {
            display().info("(saisie effacée — /quitter ou Ctrl+D pour quitter)");
            continue;
        }
        const std::string_view line = trim(input.text);
        if (line.empty()) {
            continue;
        }
        auto result = client.run("input", {{"line", std::string(line)}});
        if (!result) {
            if (client.lost()) {
                break;
            }
            display().error(core::describe(result.error()));
            continue;
        }
        if (result->value("quit", false)) {
            conversation = result->value("conversation", conversation);
            break;
        }
    }
    if (client.lost()) {
        display().error("Connexion au démon perdue (voir logs/daemon.log du dossier de données).");
        return EXIT_FAILURE;
    }
    display().farewell(shown(conversation));
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    // Seul endroit où les exceptions sont tolérées (docs/PROJECT.md § 4.2).
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        report_fatal(e.what());
    } catch (...) {
        report_fatal("exception inconnue");
    }
    return EXIT_FAILURE;
}
