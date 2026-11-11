// Interface en ligne de commande d'Avenoir.
//
// Usage : avenoir [chemin/vers/config.toml]      (défaut : config.toml)
// Logs  : variable AVENOIR_LOG (trace, debug, info, warn, error). Par défaut,
//         seuls les avertissements s'affichent ; le journal d'audit n'apparaît
//         qu'avec AVENOIR_LOG (les actions sont déjà montrées par les lignes d'action).
// Couleurs : désactivées hors d'un terminal ou si NO_COLOR est défini.
//
// Ctrl+C ne ferme pas Avenoir : il arrête l'action en cours puis propose
// d'annuler ce qui peut l'être (docs/PROJECT.md § 2.4). /quitter ou Ctrl+D quitte.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/agent.hpp"
#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/llm/factory.hpp"
#include "avenoir/llm/models.hpp"
#include "avenoir/modules/discovery.hpp"
#include "avenoir/modules/host.hpp"
#include "avenoir/storage/store.hpp"
#include "avenoir/terminal/style.hpp"
#include "avenoir/tools/tools.hpp"

#include "console.hpp"
#include "conversations.hpp"
#include "display.hpp"
#include "modules.hpp"
#include "prompt.hpp"

namespace {

using namespace avenoir;
using cli::display;
using terminal::Color;
using terminal::paint;

/// Écrit une erreur sur stderr sans pouvoir lever d'exception (dernier
/// recours, au plus haut niveau). Un échec d'écriture est ignoré.
void report_fatal(const char* message) noexcept {
    (void)std::fputs("Erreur : ", stderr);
    (void)std::fputs(message, stderr);
    (void)std::fputs("\n", stderr);
}

/// Logs sur stderr (stdout reste réservé au dialogue). Le journal d'audit
/// n'est affiché que si AVENOIR_LOG est défini.
void setup_logging() {
    const auto main_logger = spdlog::stderr_color_mt("avenoir");
    const auto audit = spdlog::stderr_color_mt("audit");
    spdlog::set_default_logger(main_logger);
    spdlog::set_pattern("%^[%n %l]%$ %v");
    spdlog::set_level(spdlog::level::warn);
    audit->set_level(spdlog::level::off);
    if (const auto level = core::read_env("AVENOIR_LOG")) {
        spdlog::set_level(spdlog::level::from_str(*level));
    }
}

/// Commandes d'Avenoir (sans « / »), alias compris : un module ne peut pas
/// en déclarer une du même nom.
std::vector<std::string> builtin_commands() {
    return {"aide",  "annuler",    "charger",   "conversations", "decharger",
            "help",  "historique", "recharger", "memoire",       "modules",
            "model", "modele",     "nouvelle",  "ouvrir",        "outils",
            "q",     "quit",       "quitter",   "raisonnement",  "renommer",
            "reset", "risque",     "supprimer", "tools",         "transcription"};
}

/// Commandes proposées à la complétion (Tab).
std::vector<std::string> command_names(const cli::ModulesUi& modules) {
    std::vector<std::string> names{
        "/aide",          "/annuler",    "/charger", "/decharger", "/recharger",
        "/conversations", "/historique", "/modele",  "/modules",   "/nouvelle",
        "/ouvrir",        "/outils",     "/memoire", "/quitter",   "/raisonnement",
        "/renommer",      "/reset",      "/risque",  "/supprimer", "/transcription"};
    std::ranges::copy(modules.completion_names(), std::back_inserter(names));
    return names;
}

void print_help(const cli::ModulesUi& modules) {
    const auto title = [](std::string_view text) {
        return paint(text, {.color = Color::White, .bold = true});
    };
    const auto command = [](std::string_view name, std::string_view description) {
        return "  " + terminal::pad(paint(name, {.color = Color::Cyan}), 26) +
               paint(description, {.color = Color::Gray});
    };
    std::string out = title("Commandes") + "\n";
    out += command("/aide", "cette aide") + "\n";
    out += command("/outils", "outils disponibles et leur niveau de risque") + "\n";
    out += command("/annuler", "annuler les actions de la dernière requête (répétable)") + "\n";
    out += command("/modele [nom]", "modèles disponibles, ou changer de modèle") + "\n";
    out +=
        command("/memoire [nom]", "modèles chargés en mémoire, et la mémoire vidéo occupée") + "\n";
    out += command("/raisonnement [niveau] [modèle]",
                   "aucun, faible, moyen, élevé ou défaut ; sans niveau : l'actuel") +
           "\n";
    out +=
        command("/charger [nom]", "charger un modèle local en mémoire (actif par défaut)") + "\n";
    out += command("/decharger [nom]", "le décharger : libère la mémoire vidéo") + "\n";
    out += command("/recharger [nom]", "le décharger puis le recharger") + "\n";
    out += command("/quitter", "quitter (ou Ctrl+D)") + "\n";
    out += title("Modules") + "\n";
    out += command("/modules", "modules installés, permissions et outils") + "\n";
    out +=
        command("/risque [outil niveau]", "voir ou changer le risque d'un outil de module") + "\n";
    out += modules.help();
    out += title("Conversations") +
           paint("  conservées entre deux lancements", {.color = Color::Gray}) + "\n";
    out += command("/nouvelle [titre]", "nouvelle conversation") + "\n";
    out += command("/reset", "efface la conversation en cours et l'écran") + "\n";
    out += command("/conversations", "liste des conversations") + "\n";
    out += command("/ouvrir <n° ou titre>", "reprendre une conversation") + "\n";
    out += command("/renommer <titre>", "renommer la conversation en cours") + "\n";
    out += command("/supprimer [n° ou titre]", "supprimer une conversation") + "\n";
    out += command("/historique", "réafficher toute la conversation") + "\n";
    out += command("/transcription", "enregistrer la conversation dans un fichier Markdown") + "\n";
    out += paint("Ctrl+C arrête l'action en cours sans quitter, puis propose d'annuler ce qui "
                 "peut l'être.",
                 {.color = Color::Gray});
    display().plain(out);
}

void print_tools(const core::Agent& agent) {
    for (const auto& spec : agent.tools().specs()) {
        display().plain(std::format("  {} {}  {}", paint(cli::kToolGlyph, {.color = Color::Yellow}),
                                    terminal::pad(paint(spec.name, {.bold = true}), 24),
                                    terminal::risk_badge(spec.risk)));
        display().plain("    " +
                        paint(terminal::ellipsize(spec.description, 66), {.color = Color::Gray}));
    }
}

/// Confirmation au terminal, appelée depuis le thread de la requête (le
/// thread principal ne lit pas l'entrée pendant ce temps).
class CliConfirmation final : public core::ConfirmationHandler {
  public:
    explicit CliConfirmation(cli::Prompt& prompt) : prompt_(&prompt) {}

    [[nodiscard]] core::Decision confirm(const core::ToolSpec& spec,
                                         const core::ToolCall& call) override {
        display().suspend_thinking();
        display().confirmation(spec, call);
        const bool approved = prompt_->ask_yes_no(
            "  " + paint("Autoriser ?", {.color = Color::Yellow, .bold = true}) + " [o/N] ");
        display().resume_thinking();
        return approved ? core::Decision::Approved : core::Decision::Rejected;
    }

  private:
    cli::Prompt* prompt_;
};

/// Propose d'annuler les actions d'un journal ; `journal` doit avoir au moins
/// une action annulable.
void offer_undo(core::Agent& agent, cli::Prompt& prompt, const core::RequestJournal& journal) {
    display().info(std::format("Actions effectuées pour « {} » :", journal.request));
    for (const auto& action : journal.actions) {
        if (action.undo) {
            display().plain("  " + paint("↺", {.color = Color::Green}) + " " +
                            action.undo->description);
        } else {
            display().plain("  " + paint("✗", {.color = Color::Gray}) + " " +
                            paint(action.summary + " (non annulable)", {.color = Color::Gray}));
        }
    }
    const auto count = journal.undoable_count();
    if (!prompt.ask_yes_no(std::format("  Annuler {} action(s) annulable(s) ? [o/N] ", count))) {
        (void)cli::take_interrupt(); // Un Ctrl+C ici vaut simplement « non ».
        display().info("Rien n'a été annulé.");
        return;
    }
    for (const auto& outcome : agent.undo_last()) {
        if (outcome.succeeded) {
            display().success(outcome.description);
        } else {
            display().error(std::format("{} : {}", outcome.description, outcome.message));
        }
    }
}

/// Travail exécuté dans le thread de la requête : agent ou commande de module.
using Work = std::function<core::Result<std::string>(const std::stop_token&)>;

/// Exécute une requête dans un thread de travail. Le thread principal anime
/// la toupie et transforme un Ctrl+C en demande d'arrêt de la requête.
/// Libellé de la toupie pendant une requête, demandé toutes les 500 ms.
using Activity = std::function<std::string()>;

void run_request(core::Agent& agent, cli::Prompt& prompt, const Work& work,
                 const Activity& activity = {}) {
    using namespace std::chrono_literals;
    (void)cli::take_interrupt(); // Oublie un Ctrl+C antérieur.

    std::optional<core::Result<std::string>> result;
    std::atomic<bool> done{false};
    display().start_thinking();
    if (activity) {
        display().set_activity(activity());
    }
    {
        std::jthread worker([&work, &result, &done](const std::stop_token& stop) {
            result = work(stop);
            done = true;
        });
        bool stop_announced = false;
        auto next_activity = std::chrono::steady_clock::now() + 500ms;
        while (!done) {
            if (activity && std::chrono::steady_clock::now() >= next_activity) {
                display().set_activity(activity());
                next_activity = std::chrono::steady_clock::now() + 500ms;
            }
            if (cli::take_interrupt() && !stop_announced) {
                worker.request_stop();
                display().warning("Arrêt demandé…");
                stop_announced = true;
            }
            display().tick();
            std::this_thread::sleep_for(50ms);
        }
    } // join
    display().stop_thinking();

    if (!result) {
        return;
    }
    if (*result) {
        display().assistant(**result);
        return;
    }
    if (result->error().code != core::ErrorCode::Cancelled) {
        display().error(core::describe(result->error()));
        return;
    }
    display().warning("Requête interrompue.");
    if (const auto* journal = agent.last_journal();
        journal != nullptr && journal->undoable_count() > 0) {
        offer_undo(agent, prompt, *journal);
    }
}

/// Repère les modèles chargés ou déchargés hors d'Avenoir (Ollama décharge un
/// modèle après une inactivité) pour l'annoncer, même pendant la saisie.
class LoadWatch {
  public:
    explicit LoadWatch(const llm::ModelSwitcher& switcher) : switcher_(&switcher) {}

    /// Après chaque requête ou commande : l'état actuel devient la référence
    /// (ce qu'Avenoir a chargé ou déchargé lui-même est déjà annoncé).
    void refresh() {
        auto now = snapshot();
        const std::scoped_lock lock(mutex_);
        known_ = std::move(now);
    }

    /// Changements depuis la dernière fois (appelée depuis un autre thread).
    [[nodiscard]] std::vector<std::string> changes() {
        auto now = snapshot();
        const std::scoped_lock lock(mutex_);
        std::vector<std::string> messages;
        for (const auto& [name, loaded] : now) {
            const auto before = known_.find(name);
            if (before == known_.end() || before->second == loaded) {
                continue;
            }
            messages.push_back(
                loaded
                    ? std::format("Modèle « {} » chargé en mémoire par un autre programme.", name)
                    : std::format("Modèle « {} » déchargé par Ollama (inactivité ou autre "
                                  "programme) : sa mémoire est libérée.",
                                  name));
        }
        known_ = std::move(now);
        return messages;
    }

  private:
    /// Modèles locaux dont on connaît l'état (les autres sont ignorés).
    [[nodiscard]] std::map<std::string, bool> snapshot() const {
        std::map<std::string, bool> states;
        for (const auto& model : switcher_->models()) {
            if (const auto loaded = switcher_->is_loaded(model)) {
                states.emplace(model.name, *loaded);
            }
        }
        return states;
    }

    const llm::ModelSwitcher* switcher_;
    std::mutex mutex_;
    std::map<std::string, bool> known_;
};

/// Toupie d'une requête au modèle actif : « Chargement du modèle « x » »
/// tant que son serveur le charge (après une inactivité, Ollama le décharge),
/// puis « Réflexion », avec un message une fois le modèle chargé. Vide si le
/// modèle est déjà chargé ou si on ne peut pas le savoir.
Activity model_activity(const llm::ModelSwitcher& switcher) {
    const auto* model = switcher.active();
    if (model == nullptr || switcher.is_loaded(*model).value_or(true)) {
        return {};
    }
    const std::string name = model->name;
    const auto since = std::chrono::steady_clock::now();
    const auto loaded = std::make_shared<bool>(false);
    return [&switcher, name, since, loaded] -> std::string {
        if (*loaded) {
            return "Réflexion";
        }
        const auto* current = switcher.find(name);
        if (current == nullptr || switcher.is_loaded(*current).value_or(true)) {
            *loaded = true;
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - since);
            display().info(std::format("Modèle « {} » chargé en {} s.", name, seconds.count()));
            return "Réflexion";
        }
        return std::format("Chargement du modèle « {} »", name);
    };
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

/// Traite une commande (ligne commençant par « / »). Renvoie faux pour quitter.
/// Clé de la base où est retenu le dernier modèle utilisé.
constexpr std::string_view kActiveModelKey = "llm.active";
/// Préfixe des clés où est retenu le raisonnement de chaque modèle.
constexpr std::string_view kReasoningKeyPrefix = "llm.reasoning.";

/// Après chaque requête ou commande : déchargement différé du modèle actif
/// (demandé pendant la requête, que sa réponse aurait rechargé).
void finish_request(llm::ModelSwitcher& switcher, LoadWatch& watch) {
    if (auto note = switcher.finish_request()) {
        display().info(*note);
    }
    watch.refresh();
}

/// Modèle actif pour le cadre d'accueil.
std::string model_label(const llm::ModelSwitcher& switcher) {
    const auto* active = switcher.active();
    return active != nullptr ? llm::describe(*active) : "aucun modèle : voie rapide seulement";
}

/// `/modele` sans argument : les modèles de la configuration, l'actif marqué.
void print_models(const llm::ModelSwitcher& switcher) {
    if (switcher.models().empty()) {
        display().info("Aucun modèle : seule la voie rapide répond. Ajoutez une section "
                       "[models.<nom>] à config.toml (voir config.example.toml).");
        return;
    }
    for (const auto& model : switcher.models()) {
        const bool active = &model == switcher.active();
        std::string line = llm::describe(model);
        if (switcher.local_only() && !model.local) {
            line += " (indisponible : mode local uniquement)";
        }
        if (std::holds_alternative<core::OpenAiCompatibleConfig>(model.config)) {
            line += ", raisonnement " + llm::reasoning_label(llm::ModelSwitcher::reasoning(model));
        }
        display().plain(
            std::format("  {} {}", active ? paint("●", {.color = Color::Green, .bold = true}) : " ",
                        paint(line, {.bold = active})));
    }
    if (switcher.models().size() < 2) {
        display().info("Un seul modèle : ajoutez-en d'autres dans config.toml ([models.<nom>], "
                       "voir config.example.toml).");
    }
}

bool run_command(core::Agent& agent, cli::Prompt& prompt, cli::Conversations& conversations,
                 cli::ModulesUi& modules, const llm::ModelSwitcher& switcher,
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
        print_help(modules);
    } else if (command == "/nouvelle") {
        conversations.create(argument);
    } else if (command == "/reset") {
        if (const auto cleared = conversations.clear()) {
            display().clear();
            display().banner(model_label(switcher), agent.tools().size(),
                             conversations.describe_current());
            display().success("Conversation effacée.");
            if (cleared->transcript) {
                display().info("Transcription : " + cli::utf8_path(*cleared->transcript));
            }
        }
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
    } else if (command == "/transcription") {
        conversations.transcribe();
    } else if (command == "/outils" || command == "/tools") {
        print_tools(agent);
    } else if (command == "/annuler") {
        if (const auto* journal = agent.last_undoable()) {
            offer_undo(agent, prompt, *journal);
        } else {
            display().info("Rien à annuler.");
        }
    } else if (command == "/modele" || command == "/model") {
        if (argument.empty()) {
            print_models(switcher);
        } else if (agent.tools().find("switch_llm") == nullptr) {
            display().info("Un seul modèle configuré : rien à changer.");
        } else {
            // Comme un appel de la voie rapide : permissions, journal (/annuler
            // revient au modèle précédent) et confirmation pour un modèle distant.
            const core::ToolCall call{
                .id = {}, .name = "switch_llm", .arguments = {{"name", std::string(argument)}}};
            const std::string input(line);
            run_request(agent, prompt, [&agent, &input, &call](const std::stop_token& stop) {
                return agent.run_tool(input, call, stop);
            });
        }
    } else if (command == "/charger" || command == "/decharger" || command == "/recharger") {
        const bool unload = command == "/decharger";
        const char* tool = unload ? "unload_llm" : "load_llm";
        if (agent.tools().find(tool) == nullptr) {
            display().info("Aucun modèle local à charger ou décharger.");
        } else {
            nlohmann::json arguments = nlohmann::json::object();
            if (!argument.empty()) {
                arguments["name"] = std::string(argument);
            }
            if (command == "/recharger") {
                arguments["reload"] = true;
            }
            const core::ToolCall call{.id = {}, .name = tool, .arguments = std::move(arguments)};
            const std::string input(line);
            std::string name = std::string(argument);
            if (name.empty()) {
                name = switcher.active() != nullptr ? switcher.active()->name : "?";
            }
            const char* verb = "Chargement";
            if (unload) {
                verb = "Déchargement";
            } else if (command == "/recharger") {
                verb = "Rechargement";
            }
            std::string label = std::format("{} du modèle « {} »", verb, name);
            run_request(
                agent, prompt,
                [&agent, &input, &call](const std::stop_token& stop) {
                    return agent.run_tool(input, call, stop);
                },
                [label = std::move(label)] { return label; });
        }
    } else if (command == "/memoire" || command == "/mémoire") {
        if (const auto text = switcher.status(argument)) {
            for (const auto& row : std::views::split(*text, '\n')) {
                display().plain("  " + std::string(std::string_view(row)));
            }
        } else {
            display().error(core::describe(text.error()));
        }
    } else if (command == "/raisonnement") {
        if (argument.empty()) {
            if (const auto* active = switcher.active()) {
                display().info(
                    std::format("Raisonnement de « {} » : {}.", active->name,
                                llm::reasoning_label(llm::ModelSwitcher::reasoning(*active))));
            } else {
                display().info("Aucun modèle.");
            }
        } else if (agent.tools().find("set_reasoning") == nullptr) {
            display().info("Aucun modèle dont le raisonnement se règle (openai_compatible).");
        } else {
            // « /raisonnement élevé » ou « /raisonnement élevé leger ».
            const auto separator = argument.find(' ');
            nlohmann::json arguments{{"level", std::string(argument.substr(0, separator))}};
            if (separator != std::string_view::npos) {
                arguments["name"] = std::string(trim(argument.substr(separator + 1)));
            }
            const core::ToolCall call{
                .id = {}, .name = "set_reasoning", .arguments = std::move(arguments)};
            const std::string input(line);
            run_request(agent, prompt, [&agent, &input, &call](const std::stop_token& stop) {
                return agent.run_tool(input, call, stop);
            });
        }
    } else if (command == "/modules") {
        modules.print_status();
    } else if (command == "/risque") {
        modules.risk(argument, prompt, conversations.store());
    } else if (const auto* own = modules.find_command(command.substr(1))) {
        // Commande d'un module : l'outil est appelé sans LLM, sous contrôle
        // des permissions, comme par la voie rapide.
        const core::ToolCall call = cli::ModulesUi::make_call(*own, argument);
        const std::string input(line);
        run_request(agent, prompt, [&agent, &input, &call](const std::stop_token& stop) {
            return agent.run_tool(input, call, stop);
        });
    } else {
        display().warning(std::format("Commande inconnue : {} (voir /aide)", command));
    }
    return true;
}

int run(int argc, char** argv) {
    setup_logging();
    cli::install_console();
    terminal::set_enabled(terminal::detect_color_support());

    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const std::string path = args.size() > 1 ? args[1] : "config.toml";
    auto config = core::load_config(path);
    if (!config) {
        display().error(core::describe(config.error()) + " (fichier : " + path +
                        ", partir de config.example.toml)");
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
            display().error(core::describe(router.error()));
            return EXIT_FAILURE;
        }
        if (auto rules = tools::register_system_rules(*router); !rules) {
            display().error(core::describe(rules.error()));
            return EXIT_FAILURE;
        }
        options.router = std::move(*router);
    }

    core::ToolRegistry tools;
    if (auto registered = tools::register_native_tools(tools); !registered) {
        display().error(core::describe(registered.error()));
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
            display().error(core::describe(opened.error()) + " — conversations non conservées");
        }
    } else {
        display().error("dossier de données introuvable — conversations non conservées");
    }

    // Modules : découverts, lancés, puis leurs outils, règles et commandes
    // rejoignent ceux d'Avenoir. Leurs messages sont montrés après l'accueil.
    std::vector<std::string> startup_warnings;
    modules::HostOptions host_options;
    if (directory) {
        host_options.data_root = *directory / "module-data";
        host_options.log_root = *directory / "logs" / "modules";
    }
    modules::ModuleHost host(host_options);
    {
        std::vector<modules::RiskOverride> overrides;
        if (store) {
            if (auto choices = store->risk_choices()) {
                for (auto& choice : *choices) {
                    overrides.push_back({.tool = std::move(choice.tool),
                                         .risk = choice.risk,
                                         .agreed = choice.agreed});
                }
            } else {
                startup_warnings.push_back(core::describe(choices.error()));
            }
        }
        const auto discovery = modules::discover(modules::default_search_directories(directory));
        for (const auto& problem : discovery.problems) {
            startup_warnings.push_back(std::format("module ignoré ({}) : {}",
                                                   problem.directory.string(), problem.message));
        }
        host.load(discovery, config->modules, overrides);
        host.register_tools(tools);
        if (options.router) {
            host.register_fast_path(*options.router);
        }
    }
    cli::ModulesUi modules_ui(host, host.commands(builtin_commands()));
    std::ranges::move(host.take_warnings(), std::back_inserter(startup_warnings));

    cli::Prompt prompt(command_names(modules_ui), history_file);
    // Surveillance des modèles pendant la saisie, branchée une fois le
    // sélecteur de modèles créé.
    std::function<std::vector<std::string>()> poll_models;
    // Chaque saisie (requête, confirmation) passe par la barre de saisie.
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
        .poll = [&poll_models] { return poll_models ? poll_models() : std::vector<std::string>{}; },
        .notify = [](const std::string& message) { display().notify(message); },
    });

    // Modèles entre lesquels basculer (/modele, outil switch_llm, voie
    // rapide) ; passer à un modèle distant se confirme (directive n° 17).
    CliConfirmation switch_confirmer(prompt);
    llm::ModelSwitcher switcher(llm::models_from(*config), &switch_confirmer);
    switcher.set_unload_on_switch(config->agent.unload_on_switch);
    switcher.set_local_only(config->agent.local_only);
    LoadWatch load_watch(switcher);
    // Modèle de départ : le dernier utilisé (retenu dans la base), sinon le
    // premier de la configuration ; aucun modèle : seule la voie rapide répond.
    std::optional<std::string> remembered;
    if (store) {
        if (auto state = store->state(kActiveModelKey)) {
            remembered = std::move(*state);
        } else {
            startup_warnings.push_back(core::describe(state.error()));
        }
    }
    // Raisonnement réglé par modèle, retenu lui aussi.
    if (store) {
        for (const auto& model : switcher.models()) {
            if (auto state = store->state(std::string(kReasoningKeyPrefix) + model.name);
                state && state->has_value()) {
                switcher.restore_reasoning(model.name, **state);
            }
        }
    }
    auto llm =
        switcher.start(remembered ? std::optional<std::string_view>(*remembered) : std::nullopt);
    if (!llm) {
        display().error(core::describe(llm.error()));
        return EXIT_FAILURE;
    }
    if (switcher.local_only() && switcher.active() == nullptr && !switcher.models().empty()) {
        startup_warnings.emplace_back(
            "mode « local uniquement » : aucun modèle local, seule la voie rapide répond");
    }
    // Outils de modèle (charger, décharger, raisonnement, bascule) selon les
    // modèles configurés, et leurs règles de voie rapide.
    for (auto& tool : llm::make_model_tools(switcher)) {
        if (auto added = tools.add(std::move(tool)); !added) {
            display().error(core::describe(added.error()));
            return EXIT_FAILURE;
        }
    }
    if (options.router) {
        if (auto rules = llm::register_model_rules(*options.router, switcher); !rules) {
            display().error(core::describe(rules.error()));
            return EXIT_FAILURE;
        }
    }

    core::Agent agent(std::move(*llm), std::move(tools), std::make_unique<CliConfirmation>(prompt),
                      std::move(options));
    switcher.attach(agent);
    if (config->agent.verify_replies) {
        agent.add_reply_check(llm::make_model_claim_check(switcher));
    }

    cli::Conversations conversations(std::move(store), agent, prompt);
    switcher.on_reasoning([&conversations](const llm::ModelChoice& model) {
        if (auto* base = conversations.store()) {
            const auto effort = llm::ModelSwitcher::reasoning(model).value_or("");
            if (auto saved = base->set_state(std::string(kReasoningKeyPrefix) + model.name, effort);
                !saved) {
                spdlog::warn("raisonnement non retenu : {}", core::describe(saved.error()));
            }
        }
    });
    // Le modèle choisi est retenu pour le prochain lancement.
    switcher.on_switch([&conversations](const llm::ModelChoice& model) {
        if (auto* base = conversations.store()) {
            if (auto saved = base->set_state(kActiveModelKey, model.name); !saved) {
                spdlog::warn("modèle actif non retenu : {}", core::describe(saved.error()));
            }
        }
    });
    // Chaque message est enregistré, et les actions sont montrées en direct.
    agent.set_history_observer(core::HistoryObserver{
        .appended =
            [&conversations](const core::Message& message) {
                conversations.record(message);
                if (const auto* reply = std::get_if<core::AssistantMessage>(&message)) {
                    display().tool_calls(reply->tool_calls);
                } else if (const auto* result = std::get_if<core::ToolResultMessage>(&message)) {
                    display().tool_result(*result);
                }
            },
        .removed_last = [&conversations] { conversations.record_removal(); },
    });
    conversations.resume();

    // Barre de saisie fixe en bas du terminal ; le terminal est rendu dans son
    // état normal à la sortie, même en cas d'erreur.
    display().enable_pinned_bar(terminal::detect_vt_support());
    struct ScreenGuard {
        ScreenGuard() = default;
        ScreenGuard(const ScreenGuard&) = delete;
        ScreenGuard& operator=(const ScreenGuard&) = delete;
        ScreenGuard(ScreenGuard&&) = delete;
        ScreenGuard& operator=(ScreenGuard&&) = delete;
        ~ScreenGuard() { display().shutdown(); }
    } const screen_guard;

    display().banner(model_label(switcher), agent.tools().size(), conversations.describe_current());
    for (const auto& warning : startup_warnings) {
        display().warning(warning);
    }
    conversations.show_recent();
    // Surveillance des modèles : état de départ, puis changements montrés
    // pendant la saisie.
    load_watch.refresh();
    poll_models = [&load_watch] { return load_watch.changes(); };

    while (true) {
        const auto input = prompt.read(cli::Display::prompt_marker(), /*remember=*/true);
        if (input.status == cli::Prompt::Status::EndOfInput) {
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
        if (line.starts_with('/')) {
            if (!run_command(agent, prompt, conversations, modules_ui, switcher, line)) {
                break;
            }
            finish_request(switcher, load_watch);
            continue;
        }
        const std::string request(line);
        run_request(
            agent, prompt,
            [&agent, &request](const std::stop_token& stop) { return agent.handle(request, stop); },
            model_activity(switcher));
        finish_request(switcher, load_watch);
    }
    display().farewell(conversations.describe_current());
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    // Seul endroit où les exceptions sont tolérées (docs/PROJECT.md § 4.2) :
    // elles sont toutes arrêtées ici.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        report_fatal(e.what());
    } catch (...) {
        report_fatal("exception inconnue");
    }
    return EXIT_FAILURE;
}
