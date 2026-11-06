// Interface en ligne de commande d'Avenoir.
//
// Usage : avenoir [chemin/vers/config.toml]      (défaut : config.toml)
// Logs  : variable AVENOIR_LOG (trace, debug, info, warn, error ; défaut warn).
//
// Ctrl+C ne ferme pas Avenoir : il arrête l'action en cours puis propose
// d'annuler ce qui peut l'être (PROJECT.md § 2.4). /quitter ou Ctrl+D quitte.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/agent.hpp"
#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/core/version.hpp"
#include "avenoir/llm/factory.hpp"
#include "avenoir/storage/store.hpp"
#include "avenoir/tools/tools.hpp"

#include "console.hpp"
#include "conversations.hpp"
#include "prompt.hpp"

namespace {

using namespace avenoir;

/// Écrit une erreur sur stderr sans pouvoir lever d'exception. Un échec
/// d'écriture est ignoré : il n'y a plus rien à faire à ce stade.
void report_error(const char* message) noexcept {
    (void)std::fputs("Erreur : ", stderr);
    (void)std::fputs(message, stderr);
    (void)std::fputs("\n", stderr);
}

/// Logs sur stderr (stdout reste réservé au dialogue). Le journal d'audit a
/// son propre logger, au niveau info.
void setup_logging() {
    const auto main_logger = spdlog::stderr_color_mt("avenoir");
    spdlog::set_default_logger(main_logger);
    spdlog::set_level(spdlog::level::warn);
    if (const auto level = core::read_env("AVENOIR_LOG")) {
        spdlog::set_level(spdlog::level::from_str(*level));
    }
    const auto audit = spdlog::stderr_color_mt("audit");
    audit->set_level(spdlog::level::info);
}

/// Commandes proposées à la complétion (Tab).
std::vector<std::string> command_names() {
    return {"/aide",   "/annuler", "/conversations", "/historique", "/nouvelle", "/ouvrir",
            "/outils", "/quitter", "/renommer",      "/reset",      "/supprimer"};
}

void print_help() {
    std::println("Commandes :");
    std::println("  /aide       cette aide");
    std::println("  /outils     outils disponibles et leur niveau de risque");
    std::println("  /annuler    annuler les actions de la dernière requête (répétable)");
    std::println("  /quitter    quitter (ou Ctrl+D)");
    std::println("Conversations (conservées entre deux lancements) :");
    std::println("  /nouvelle [titre]        nouvelle conversation (/reset : idem)");
    std::println("  /conversations           liste des conversations");
    std::println("  /ouvrir <n° ou titre>    reprendre une conversation");
    std::println("  /renommer <titre>        renommer la conversation en cours");
    std::println("  /supprimer [n° ou titre] supprimer une conversation (en cours par défaut)");
    std::println("  /historique              réafficher toute la conversation");
    std::println("Ctrl+C arrête l'action en cours sans quitter, puis propose d'annuler ce qui peut "
                 "l'être.");
}

/// Confirmation au terminal, appelée depuis le thread de la requête (le
/// thread principal ne lit pas l'entrée pendant ce temps).
class CliConfirmation final : public core::ConfirmationHandler {
  public:
    explicit CliConfirmation(cli::Prompt& prompt) : prompt_(&prompt) {}

    [[nodiscard]] core::Decision confirm(const core::ToolSpec& spec,
                                         const core::ToolCall& call) override {
        std::println("\n⚠  Action à confirmer : {}   (risque : {})", spec.name,
                     core::to_string(spec.risk));
        std::println("   {}", spec.description);
        std::println("   Arguments : {}", call.arguments.dump());
        return prompt_->ask_yes_no("   Autoriser ? [o/N] ") ? core::Decision::Approved
                                                            : core::Decision::Rejected;
    }

  private:
    cli::Prompt* prompt_;
};

/// Propose d'annuler les actions d'un journal ; `journal` doit avoir au moins
/// une action annulable.
void offer_undo(core::Agent& agent, cli::Prompt& prompt, const core::RequestJournal& journal) {
    std::println("Actions effectuées pour « {} » :", journal.request);
    for (const auto& action : journal.actions) {
        if (action.undo) {
            std::println("  ✓ {}", action.undo->description);
        } else {
            std::println("  ✗ {} (non annulable)", action.summary);
        }
    }
    const auto count = journal.undoable_count();
    if (!prompt.ask_yes_no(std::format("Annuler {} action(s) annulable(s) ? [o/N] ", count))) {
        (void)cli::take_interrupt(); // Un Ctrl+C ici vaut simplement « non ».
        std::println("Rien n'a été annulé.");
        return;
    }
    for (const auto& outcome : agent.undo_last()) {
        if (outcome.succeeded) {
            std::println("  ✓ {}", outcome.description);
        } else {
            std::println("  ✗ {} : {}", outcome.description, outcome.message);
        }
    }
}

/// Exécute une requête dans un thread de travail. Le thread principal attend
/// et transforme un Ctrl+C en demande d'arrêt de la requête.
void run_request(core::Agent& agent, cli::Prompt& prompt, const std::string& line) {
    using namespace std::chrono_literals;
    (void)cli::take_interrupt(); // Oublie un Ctrl+C antérieur.

    std::optional<core::Result<std::string>> result;
    std::atomic<bool> done{false};
    {
        std::jthread worker([&agent, &line, &result, &done](const std::stop_token& stop) {
            result = agent.handle(line, stop);
            done = true;
        });
        bool stop_announced = false;
        while (!done) {
            if (cli::take_interrupt() && !stop_announced) {
                worker.request_stop();
                std::println("\n⏹  Arrêt demandé…");
                stop_announced = true;
            }
            std::this_thread::sleep_for(50ms);
        }
    } // join

    if (!result) {
        return;
    }
    if (*result) {
        std::println("{}", **result);
        return;
    }
    if (result->error().code != core::ErrorCode::Cancelled) {
        report_error(core::describe(result->error()).c_str());
        return;
    }
    std::println("Requête interrompue.");
    if (const auto* journal = agent.last_journal();
        journal != nullptr && journal->undoable_count() > 0) {
        offer_undo(agent, prompt, *journal);
    }
}

std::string_view trim(std::string_view text);

/// Traite une commande (ligne commençant par « / »). Renvoie faux pour quitter.
bool run_command(core::Agent& agent, cli::Prompt& prompt, cli::Conversations& conversations,
                 std::string_view line) {
    // « /commande argument libre »
    const auto space = line.find(' ');
    const std::string_view command = line.substr(0, space);
    const std::string_view argument =
        space == std::string_view::npos ? std::string_view{} : trim(line.substr(space + 1));

    if (command == "/quitter" || command == "/quit" || command == "/q") {
        return false;
    }
    if (command == "/aide" || command == "/help") {
        print_help();
    } else if (command == "/nouvelle" || command == "/reset") {
        conversations.create(argument);
    } else if (command == "/conversations") {
        conversations.list();
    } else if (command == "/ouvrir") {
        conversations.open(argument);
    } else if (command == "/renommer") {
        conversations.rename(argument);
    } else if (command == "/supprimer") {
        conversations.remove(argument);
    } else if (command == "/historique") {
        conversations.show_history();
    } else if (command == "/outils" || command == "/tools") {
        for (const auto& spec : agent.tools().specs()) {
            std::println("- {} [{}] : {}", spec.name, core::to_string(spec.risk), spec.description);
        }
    } else if (command == "/annuler") {
        if (const auto* journal = agent.last_undoable()) {
            offer_undo(agent, prompt, *journal);
        } else {
            std::println("Rien à annuler.");
        }
    } else {
        std::println("Commande inconnue : {} (voir /aide)", command);
    }
    return true;
}

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

    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const std::string path = args.size() > 1 ? args[1] : "config.toml";
    auto config = core::load_config(path);
    if (!config) {
        report_error((core::describe(config.error()) + " (fichier : " + path +
                      ", partir de config.example.toml)")
                         .c_str());
        return EXIT_FAILURE;
    }

    auto llm = llm::make_provider(config->llm);
    if (!llm) {
        report_error(core::describe(llm.error()).c_str());
        return EXIT_FAILURE;
    }

    core::AgentOptions options;
    options.policy = config->permissions;
    options.max_steps = config->agent.max_steps;
    if (config->agent.system_prompt) {
        options.system_prompt = *config->agent.system_prompt;
    }
    if (config->agent.fast_path) {
        auto router = core::FastRouter::with_defaults();
        if (!router) {
            report_error(core::describe(router.error()).c_str());
            return EXIT_FAILURE;
        }
        options.router = std::move(*router);
    }

    core::ToolRegistry tools;
    if (auto registered = tools::register_native_tools(tools); !registered) {
        report_error(core::describe(registered.error()).c_str());
        return EXIT_FAILURE;
    }

    // Dossier de données : historique des saisies et base des conversations.
    const auto directory = storage::data_directory();
    std::optional<std::filesystem::path> history_file;
    std::optional<storage::Store> store;
    if (directory) {
        history_file = *directory / "cli_history.txt";
        auto opened = storage::Store::open(*directory / "avenoir.db");
        if (opened) {
            store = std::move(*opened);
        } else {
            report_error(
                (core::describe(opened.error()) + " — conversations non conservées").c_str());
        }
    } else {
        report_error("dossier de données introuvable — conversations non conservées");
    }
    cli::Prompt prompt(command_names(), history_file);

    core::Agent agent(std::move(*llm), std::move(tools), std::make_unique<CliConfirmation>(prompt),
                      std::move(options));

    std::println("avenoir {} — LLM : {} — {} outil(s). /aide pour les commandes.", core::version(),
                 agent.llm_name(), agent.tools().size());

    cli::Conversations conversations(std::move(store), agent, prompt);
    conversations.resume();
    std::println("\nConversation : {}", conversations.describe_current());

    while (true) {
        std::println("");
        const auto input = prompt.read("> ", /*remember=*/true);
        if (input.status == cli::Prompt::Status::EndOfInput) {
            break;
        }
        if (input.status == cli::Prompt::Status::Interrupted) {
            std::println("(saisie effacée — /quitter ou Ctrl+D pour quitter)");
            continue;
        }
        const std::string_view line = trim(input.text);
        if (line.empty()) {
            continue;
        }
        if (line.starts_with('/')) {
            if (!run_command(agent, prompt, conversations, line)) {
                break;
            }
            continue;
        }
        run_request(agent, prompt, std::string(line));
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    // Seul endroit où les exceptions sont tolérées (PROJECT.md § 4.2) :
    // elles sont toutes arrêtées ici.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        report_error(e.what());
    } catch (...) {
        report_error("exception inconnue");
    }
    return EXIT_FAILURE;
}
