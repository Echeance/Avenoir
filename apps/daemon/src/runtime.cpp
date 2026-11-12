#include "runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "avenoir/agents/delegate.hpp"
#include "avenoir/agents/orchestrator.hpp"
#include "avenoir/agents/plan.hpp"
#include "avenoir/core/agent.hpp"
#include "avenoir/core/claims.hpp"
#include "avenoir/core/config.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/llm/factory.hpp"
#include "avenoir/llm/models.hpp"
#include "avenoir/modules/discovery.hpp"
#include "avenoir/modules/host.hpp"
#include "avenoir/storage/store.hpp"
#include "avenoir/terminal/style.hpp"
#include "avenoir/tools/searxng.hpp"
#include "avenoir/tools/tools.hpp"
#include "avenoir/tools/web.hpp"

#include "autostart.hpp"
#include "conversations.hpp"
#include "modules.hpp"
#include "settings_catalog.hpp"
#include "setup.hpp"
#include "ui.hpp"

namespace avenoir::daemon {

namespace {

using terminal::Color;
using terminal::paint;

/// Clé de la base où est retenu le dernier modèle utilisé.
constexpr std::string_view kActiveModelKey = "llm.active";
/// Préfixe des clés où est retenu le raisonnement de chaque modèle.
constexpr std::string_view kReasoningKeyPrefix = "llm.reasoning.";
/// Clé de la base où est retenu le moteur de la recherche web.
constexpr std::string_view kSearchEngineKey = "web.engine";

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// Commandes d'Avenoir (sans « / »), alias compris : un module ne peut pas
/// en déclarer une du même nom.
std::vector<std::string> builtin_commands() {
    return {"agent",      "agents",        "aide",      "annuler",      "arreter",  "charger",
            "configurer", "conversations", "decharger", "demarrage",    "help",     "historique",
            "memoire",    "model",         "modele",    "modules",      "moteur",   "nouvelle",
            "ouvrir",     "outils",        "q",         "quit",         "quitter",  "raisonnement",
            "recharger",  "redemarrer",    "reglage",   "reglages",     "renommer", "reset",
            "risque",     "supprimer",     "tools",     "transcription"};
}

/// Confirmation d'une action par le client de la requête en cours.
class UiConfirmation final : public core::ConfirmationHandler {
  public:
    [[nodiscard]] core::Decision confirm(const core::ToolSpec& spec,
                                         const core::ToolCall& call) override {
        return display().confirm(spec, call) ? core::Decision::Approved : core::Decision::Rejected;
    }
};

/// Repère les modèles chargés ou déchargés hors d'Avenoir (Ollama décharge un
/// modèle après une inactivité) pour l'annoncer, même pendant la saisie.
class LoadWatch {
  public:
    explicit LoadWatch(const llm::ModelSwitcher& switcher) : switcher_(&switcher) {}

    /// Après chaque saisie : l'état actuel devient la référence (ce qu'Avenoir
    /// a chargé ou déchargé lui-même est déjà annoncé).
    void refresh() {
        auto now = snapshot();
        const std::scoped_lock lock(mutex_);
        known_ = std::move(now);
    }

    /// Changements depuis la dernière fois (appelée depuis un autre fil).
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

/// Travail d'une requête : agent ou commande de module.
using Work = std::function<core::Result<std::string>(const std::stop_token&)>;
/// Libellé de la toupie pendant une requête, demandé toutes les 500 ms.
using Activity = std::function<std::string()>;

/// Toupie d'une requête au modèle actif : « Chargement du modèle « x » »
/// tant que son serveur le charge, puis « Réflexion », avec un message une
/// fois le modèle chargé. Vide si le modèle est déjà chargé.
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

/// Propose d'annuler les actions d'un journal ; `journal` doit avoir au moins
/// une action annulable.
void offer_undo(core::Agent& agent, const core::RequestJournal& journal) {
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
    if (!display().ask_yes_no(std::format("  Annuler {} action(s) annulable(s) ? [o/N]", count))) {
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

/// Exécute une requête dans ce fil (celui de la requête du client) : la
/// toupie tourne chez le client, son libellé est tenu à jour ; Ctrl+C chez le
/// client demande l'arrêt (`stop`), puis l'annulation est proposée.
void run_request(core::Agent& agent, const Work& work, const std::stop_token& stop,
                 const Activity& activity = {}) {
    using namespace std::chrono_literals;
    Ui& ui = display();
    ui.start_thinking();
    std::optional<core::Result<std::string>> result;
    {
        std::jthread labels;
        if (activity) {
            ui.set_activity(activity());
            labels = std::jthread([&ui, &activity](const std::stop_token& done) {
                const UiScope scope(ui);
                while (!done.stop_requested()) {
                    std::this_thread::sleep_for(500ms);
                    if (!done.stop_requested()) {
                        ui.set_activity(activity());
                    }
                }
            });
        }
        result = work(stop);
    } // fin du fil des libellés
    ui.stop_thinking();

    if (*result) {
        // Déjà montrée au fil de l'eau : pas réaffichée.
        if (!ui.streamed()) {
            ui.assistant(**result);
        }
        return;
    }
    if (result->error().code != core::ErrorCode::Cancelled) {
        ui.error(core::describe(result->error()));
        return;
    }
    ui.warning("Requête interrompue.");
    if (const auto* journal = agent.last_journal();
        journal != nullptr && journal->undoable_count() > 0) {
        offer_undo(agent, *journal);
    }
}

/// Modèle actif pour le cadre d'accueil.
std::string model_label(const llm::ModelSwitcher& switcher) {
    const auto* active = switcher.active();
    return active != nullptr ? llm::describe(*active) : "aucun modèle : voie rapide seulement";
}

} // namespace

struct Runtime::State {
    ConfigFile file;
    std::vector<std::string> startup_warnings;
    std::unique_ptr<modules::ModuleHost> host;
    std::unique_ptr<ModulesUi> modules_ui;
    UiConfirmation switch_confirmer;
    std::unique_ptr<llm::ModelSwitcher> switcher;
    std::unique_ptr<LoadWatch> load_watch;
    // Instance SearXNG locale : déclarée avant les moteurs et l'agent, elle
    // est détruite après eux.
    std::unique_ptr<tools::SearxngHost> searxng_host;
    std::unique_ptr<tools::SearchEngines> engines;
    core::Agent* page_reader = nullptr;
    /// Outils que les agents d'une équipe peuvent recevoir (sans la délégation
    /// ni ce qui change le modèle ou le moteur de tout Avenoir).
    core::ToolRegistry agent_tools;
    std::string base_prompt; ///< Prompt système commun, repris par les agents.
    std::unique_ptr<core::Agent> agent;
    std::unique_ptr<Conversations> conversations;

    void print_help() const;
    void print_tools() const;
    void print_models() const;
    void configure(const std::stop_token& stop);
    void print_settings() const;
    void print_agents() const;
    void edit_agent(std::string_view argument);
    [[nodiscard]] agents::Environment agent_environment() const;
    static void startup(std::string_view argument);
    void change_setting(std::string_view argument);
    [[nodiscard]] Outcome command(std::string_view line, const std::stop_token& stop);
    void run_tool(std::string_view line, core::ToolCall call, const std::stop_token& stop,
                  const Activity& activity = {}) const;
};

namespace {

core::Result<void> add_tools(core::ToolRegistry& registry,
                             std::vector<std::shared_ptr<core::Tool>> tools) {
    for (auto& tool : tools) {
        if (auto added = registry.add(std::move(tool)); !added) {
            return added;
        }
    }
    return {};
}

} // namespace

core::Result<std::unique_ptr<Runtime>> Runtime::create(ConfigFile file) {
    auto s = std::make_unique<State>();
    s->file = std::move(file);
    const core::AppConfig& config = s->file.config;

    core::AgentOptions options;
    options.policy = config.permissions;
    options.max_steps = config.agent.max_steps;
    if (config.agent.system_prompt) {
        options.system_prompt = *config.agent.system_prompt;
    }
    if (config.agent.fast_path) {
        auto router = core::FastRouter::with_defaults();
        if (!router) {
            return std::unexpected(std::move(router.error()));
        }
        if (auto rules = tools::register_system_rules(*router); !rules) {
            return std::unexpected(std::move(rules.error()));
        }
        options.router = std::move(*router);
    }
    core::ToolRegistry registry;
    if (auto registered = tools::register_native_tools(registry); !registered) {
        return std::unexpected(std::move(registered.error()));
    }

    // Dossier de données : base des conversations.
    const auto directory = storage::data_directory();
    std::optional<storage::Store> store;
    if (directory) {
        auto opened = storage::Store::open(*directory / "avenoir.db");
        if (opened) {
            store = std::move(*opened);
        } else {
            s->startup_warnings.push_back(core::describe(opened.error()) +
                                          " — conversations non conservées");
        }
    } else {
        s->startup_warnings.emplace_back(
            "dossier de données introuvable — conversations non conservées");
    }

    // Modules : découverts, lancés, puis leurs outils, règles et commandes
    // rejoignent ceux d'Avenoir.
    modules::HostOptions host_options;
    if (directory) {
        host_options.data_root = *directory / "module-data";
        host_options.log_root = *directory / "logs" / "modules";
    }
    s->host = std::make_unique<modules::ModuleHost>(host_options);
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
                s->startup_warnings.push_back(core::describe(choices.error()));
            }
        }
        const auto discovery = modules::discover(modules::default_search_directories(directory));
        for (const auto& problem : discovery.problems) {
            s->startup_warnings.push_back(std::format(
                "module ignoré ({}) : {}", utf8_path(problem.directory), problem.message));
        }
        s->host->load(discovery, config.modules, overrides);
        s->host->register_tools(registry);
        if (options.router) {
            s->host->register_fast_path(*options.router);
        }
    }
    s->modules_ui = std::make_unique<ModulesUi>(*s->host, s->host->commands(builtin_commands()));
    std::ranges::move(s->host->take_warnings(), std::back_inserter(s->startup_warnings));

    // Modèles entre lesquels basculer ; passer à un modèle distant se
    // confirme (directive n° 17).
    s->switcher =
        std::make_unique<llm::ModelSwitcher>(llm::models_from(config), &s->switch_confirmer);
    s->switcher->set_unload_on_switch(config.agent.unload_on_switch);
    s->switcher->set_local_only(config.agent.local_only);
    s->load_watch = std::make_unique<LoadWatch>(*s->switcher);
    std::optional<std::string> remembered;
    if (store) {
        if (auto state = store->state(kActiveModelKey)) {
            remembered = std::move(*state);
        } else {
            s->startup_warnings.push_back(core::describe(state.error()));
        }
    }
    if (config.web.local_searxng && !config.web.searxng_url && directory) {
        s->searxng_host = std::make_unique<tools::SearxngHost>(tools::SearxngOptions{
            .config_dir = *directory / "searxng",
            .idle = std::chrono::minutes{config.web.searxng_idle_minutes},
            .notify = [](std::string_view message) { display().info(message); },
            .run = {},
            .ready = {},
        });
    }
    s->engines = std::make_unique<tools::SearchEngines>(config.web.searxng_url,
                                                        s->searxng_host.get(), config.web.engine);
    if (store) {
        if (auto state = store->state(kSearchEngineKey); state && state->has_value()) {
            s->engines->restore(**state);
        }
        for (const auto& model : s->switcher->models()) {
            if (auto state = store->state(std::string(kReasoningKeyPrefix) + model.name);
                state && state->has_value()) {
                s->switcher->restore_reasoning(model.name, **state);
            }
        }
    }
    auto llm = s->switcher->start(remembered ? std::optional<std::string_view>(*remembered)
                                             : std::nullopt);
    if (!llm) {
        return std::unexpected(std::move(llm.error()));
    }
    if (s->switcher->local_only() && s->switcher->active() == nullptr &&
        !s->switcher->models().empty()) {
        s->startup_warnings.emplace_back(
            "mode « local uniquement » : aucun modèle local, seule la voie rapide répond");
    }
    if (auto added = add_tools(registry, llm::make_model_tools(*s->switcher)); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (options.router) {
        if (auto rules = llm::register_model_rules(*options.router, *s->switcher); !rules) {
            return std::unexpected(std::move(rules.error()));
        }
    }
    // Recherche web et lecture de pages ; une page est lue à part par le
    // modèle actif, sans outils (Agent::ask_aside).
    const State* state = s.get();
    auto web_tools = tools::make_web_tools(
        *s->engines,
        [state](std::string_view instructions, std::string text,
                const std::stop_token& stop) -> core::Result<std::string> {
            if (state->page_reader == nullptr) {
                return core::make_error(core::ErrorCode::Config, "lecteur indisponible");
            }
            return state->page_reader->ask_aside(instructions, std::move(text), stop);
        });
    if (auto added = add_tools(registry, std::move(web_tools)); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (options.router) {
        if (auto rules = tools::register_web_rules(*options.router); !rules) {
            return std::unexpected(std::move(rules.error()));
        }
    }

    // Orchestrateur multi-agents (docs/PROJECT.md § 2.11) : les agents ne
    // reçoivent jamais la délégation, ni ce qui change le modèle ou le moteur
    // de recherche de tout Avenoir.
    for (const auto& spec : registry.specs()) {
        if (spec.name == "switch_llm" || spec.name == "load_llm" || spec.name == "unload_llm" ||
            spec.name == "set_reasoning" || spec.name == "set_search_engine") {
            continue;
        }
        if (auto added = s->agent_tools.add(registry.find(spec.name)); !added) {
            return std::unexpected(std::move(added.error()));
        }
    }
    for (const auto& profile : config.agents) {
        for (const auto& tool : profile.tools) {
            if (!s->agent_tools.find(tool)) {
                s->startup_warnings.push_back(
                    std::format("agent « {} » : outil « {} » indisponible pour les agents",
                                profile.name, tool));
            }
        }
    }
    s->base_prompt = options.system_prompt;
    if (config.orchestration.enabled) {
        agents::DelegateOptions delegate;
        for (const auto& profile : config.agents) {
            delegate.configured.push_back(agents::AgentSpec{.name = profile.name,
                                                            .role = profile.role,
                                                            .instructions = profile.instructions,
                                                            .model = profile.model,
                                                            .tools = profile.tools,
                                                            .max_steps = profile.max_steps,
                                                            .ephemeral = false});
        }
        delegate.allowed_tools = [state] {
            std::vector<std::string> names;
            for (const auto& spec : state->agent_tools.specs()) {
                names.push_back(spec.name);
            }
            return names;
        };
        delegate.mode = agents::Mode::Both;
        if (config.orchestration.mode == "auto") {
            delegate.mode = agents::Mode::Automatic;
        } else if (config.orchestration.mode == "configured") {
            delegate.mode = agents::Mode::Configured;
        }
        delegate.limits = agents::Limits{.max_tasks = config.orchestration.max_tasks,
                                         .max_agents = config.orchestration.max_agents,
                                         .max_parallel = config.orchestration.max_parallel,
                                         .max_tools_per_agent = 16};
        delegate.environment = [state] { return state->agent_environment(); };
        delegate.on_finished = [](const std::vector<agents::TaskOutcome>& outcomes) {
            for (const auto& outcome : outcomes) {
                if (outcome.status == agents::TaskOutcome::Status::Done) {
                    display().info(
                        std::format("◇ {} : tâche « {} » faite", outcome.agent, outcome.id));
                } else {
                    display().warning(std::format("{} : tâche « {} » non aboutie : {}",
                                                  outcome.agent, outcome.id,
                                                  terminal::ellipsize(outcome.output, 120)));
                }
            }
        };
        if (auto added = registry.add(agents::make_delegate_tool(std::move(delegate))); !added) {
            return std::unexpected(std::move(added.error()));
        }
    }

    s->agent =
        std::make_unique<core::Agent>(std::move(*llm), std::move(registry),
                                      std::make_unique<UiConfirmation>(), std::move(options));
    s->page_reader = s->agent.get();
    s->switcher->attach(*s->agent);
    if (config.agent.verify_replies) {
        const core::Agent* agent = s->agent.get();
        s->agent->add_reply_check(llm::make_model_claim_check(*s->switcher));
        s->agent->add_reply_check(core::make_claim_check(tools::action_claim_rules(
            [agent](std::string_view name) { return agent->tools().find(name) != nullptr; })));
    }

    s->conversations = std::make_unique<Conversations>(std::move(store), *s->agent);
    Conversations* conversations = s->conversations.get();
    s->switcher->on_reasoning([conversations](const llm::ModelChoice& model) {
        if (auto* base = conversations->store()) {
            const auto effort = llm::ModelSwitcher::reasoning(model).value_or("");
            if (auto saved = base->set_state(std::string(kReasoningKeyPrefix) + model.name, effort);
                !saved) {
                spdlog::warn("raisonnement non retenu : {}", core::describe(saved.error()));
            }
        }
    });
    s->engines->on_change([conversations](const std::string& engine) {
        if (auto* base = conversations->store()) {
            if (auto saved = base->set_state(kSearchEngineKey, engine); !saved) {
                spdlog::warn("moteur de recherche non retenu : {}", core::describe(saved.error()));
            }
        }
    });
    s->switcher->on_switch([conversations](const llm::ModelChoice& model) {
        if (auto* base = conversations->store()) {
            if (auto saved = base->set_state(kActiveModelKey, model.name); !saved) {
                spdlog::warn("modèle actif non retenu : {}", core::describe(saved.error()));
            }
        }
    });
    // Chaque message est enregistré, et les actions sont montrées en direct.
    s->agent->set_history_observer(core::HistoryObserver{
        .appended =
            [conversations](const core::Message& message) {
                conversations->record(message);
                if (const auto* reply = std::get_if<core::AssistantMessage>(&message)) {
                    display().tool_calls(reply->tool_calls);
                } else if (const auto* result = std::get_if<core::ToolResultMessage>(&message)) {
                    display().tool_result(*result);
                }
            },
        .removed_last = [conversations] { conversations->record_removal(); },
    });
    s->conversations->resume();
    s->load_watch->refresh();
    return std::unique_ptr<Runtime>(new Runtime(std::move(s)));
}

Runtime::Runtime(std::unique_ptr<State> state) : state_(std::move(state)) {}

Runtime::~Runtime() = default;

const ConfigFile& Runtime::config() const {
    return state_->file;
}

void Runtime::welcome(bool first) {
    display().banner(model_label(*state_->switcher), state_->agent->tools().size(),
                     state_->conversations->describe_current());
    if (first) {
        for (const auto& warning : state_->startup_warnings) {
            display().warning(warning);
        }
    }
    state_->conversations->show_recent();
}

std::vector<std::string> Runtime::completions() const {
    std::vector<std::string> names;
    for (const auto& command : builtin_commands()) {
        if (command != "q" && command != "quit" && command != "help" && command != "tools" &&
            command != "model") {
            names.push_back("/" + command);
        }
    }
    std::ranges::copy(state_->modules_ui->completion_names(), std::back_inserter(names));
    return names;
}

std::string Runtime::describe_conversation() const {
    return state_->conversations->describe_current();
}

void Runtime::finish_input() {
    if (auto note = state_->switcher->finish_request()) {
        display().info(*note);
    }
    state_->load_watch->refresh();
}

std::vector<std::string> Runtime::model_changes() {
    return state_->load_watch->changes();
}

Outcome Runtime::input(std::string_view line, const std::stop_token& stop) {
    line = trim(line);
    if (line.empty()) {
        return Outcome::Continue;
    }
    if (line.starts_with('/')) {
        return state_->command(line, stop);
    }
    const std::string request(line);
    core::Agent& agent = *state_->agent;
    run_request(
        agent,
        [&agent, &request](const std::stop_token& token) {
            return agent.handle(
                request, token,
                core::ReplyStream{
                    .text = [](std::string_view fragment) { display().stream_text(fragment); },
                    .end = [](bool kept) { display().stream_end(kept); },
                });
        },
        stop, model_activity(*state_->switcher));
    return Outcome::Continue;
}

void Runtime::State::run_tool(std::string_view line, core::ToolCall call,
                              const std::stop_token& stop, const Activity& activity) const {
    const std::string input(line);
    core::Agent& target = *agent;
    run_request(
        target,
        [&target, &input, &call](const std::stop_token& token) {
            return target.run_tool(input, call, token);
        },
        stop, activity);
}

void Runtime::State::print_help() const {
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
    out += command("/moteur [nom]", "moteur de la recherche web (duckduckgo, searxng)") + "\n";
    out +=
        command("/memoire [nom]", "modèles chargés en mémoire, et la mémoire vidéo occupée") + "\n";
    out += command("/raisonnement [niveau] [modèle]",
                   "aucun, faible, moyen, élevé ou défaut ; sans niveau : l'actuel") +
           "\n";
    out +=
        command("/charger [nom]", "charger un modèle local en mémoire (actif par défaut)") + "\n";
    out += command("/decharger [nom]", "le décharger : libère la mémoire vidéo") + "\n";
    out += command("/recharger [nom]", "le décharger puis le recharger") + "\n";
    out += command("/configurer", "rechercher les modèles et choisir les siens (création guidée)") +
           "\n";
    out += command("/reglages", "réglages d'Avenoir et leur valeur") + "\n";
    out += command("/reglage <clé> <valeur>", "changer un réglage") + "\n";
    out += command("/agents", "équipe d'agents et bornes de l'orchestrateur") + "\n";
    out +=
        command("/agent <action> <nom> …", "composer l'équipe (ajouter, outils, modele…)") + "\n";
    out += command("/redemarrer", "redémarrer Avenoir avec ses réglages actuels") + "\n";
    out += command("/arreter", "arrêter Avenoir (le démon), pas seulement cette fenêtre") + "\n";
    out += command("/demarrage [oui|non]", "lancer Avenoir à l'ouverture de session") + "\n";
    out += command("/quitter", "quitter cette fenêtre (ou Ctrl+D) ; Avenoir continue") + "\n";
    out += title("Modules") + "\n";
    out += command("/modules", "modules installés, permissions et outils") + "\n";
    out +=
        command("/risque [outil niveau]", "voir ou changer le risque d'un outil de module") + "\n";
    out += modules_ui->help();
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

void Runtime::State::print_tools() const {
    for (const auto& spec : agent->tools().specs()) {
        display().plain(std::format("  {} {}  {}", paint(kToolGlyph, {.color = Color::Yellow}),
                                    terminal::pad(paint(spec.name, {.bold = true}), 24),
                                    terminal::risk_badge(spec.risk)));
        display().plain("    " +
                        paint(terminal::ellipsize(spec.description, 66), {.color = Color::Gray}));
    }
}

void Runtime::State::print_models() const {
    if (switcher->models().empty()) {
        display().info("Aucun modèle : seule la voie rapide répond. /configurer pour en ajouter.");
        return;
    }
    for (const auto& model : switcher->models()) {
        const bool active = &model == switcher->active();
        std::string line = llm::describe(model);
        if (switcher->local_only() && !model.local) {
            line += " (indisponible : mode local uniquement)";
        }
        if (std::holds_alternative<core::OpenAiCompatibleConfig>(model.config)) {
            line += ", raisonnement " + llm::reasoning_label(llm::ModelSwitcher::reasoning(model));
        }
        display().plain(
            std::format("  {} {}", active ? paint("●", {.color = Color::Green, .bold = true}) : " ",
                        paint(line, {.bold = active})));
    }
    if (switcher->models().size() < 2) {
        display().info("Un seul modèle : /configurer pour en ajouter d'autres.");
    }
}

void Runtime::State::configure(const std::stop_token& stop) {
    std::error_code error;
    if (std::filesystem::exists(file.path, error)) {
        display().info(std::format("Configuration des modèles : {} sera réécrit (les autres "
                                   "réglages sont gardés, les commentaires du fichier perdus).",
                                   utf8_path(file.path)));
        if (!display().ask_yes_no("  Continuer ? [o/N]")) {
            display().info("Configuration inchangée.");
            return;
        }
    }
    const SetupAsk ask = [](const std::string& question) { return display().ask(question); };
    const auto result = run_setup(file.path, file.config, ask, file.save, stop);
    if (!result) {
        display().info("Configuration inchangée.");
        return;
    }
    if (result->saved) {
        file.config = result->config;
        display().info("Pour l'utiliser dès maintenant : /redemarrer.");
    }
}

void Runtime::State::print_settings() const {
    display().plain(paint("Réglages", {.color = Color::White, .bold = true}) + "  " +
                    paint(utf8_path(file.path), {.color = Color::Gray}));
    for (const auto& line : list_settings(file.config, modules_ui->known_modules())) {
        const std::string mark = line.security ? paint("▲", {.color = Color::Yellow}) : " ";
        display().plain(std::format("  {} {} {}", mark,
                                    terminal::pad(paint(line.key, {.color = Color::Cyan}), 30),
                                    paint(line.value, {.bold = true})));
        display().plain(paint(std::format("      {} ({})", line.description, line.accepted),
                              {.color = Color::Gray}));
    }
    display().plain(paint("Changer : /reglage <clé> <valeur>, puis /redemarrer pour l'appliquer. "
                          "▲ : l'affaiblir demande votre accord.",
                          {.color = Color::Gray}));
}

void Runtime::State::change_setting(std::string_view argument) {
    const auto space = argument.find(' ');
    const std::string_view key = argument.substr(0, space);
    const std::string_view value =
        space == std::string_view::npos ? std::string_view{} : trim(argument.substr(space + 1));
    if (key.empty() || value.empty()) {
        display().warning("Usage : /reglage <clé> <valeur> (voir /reglages)");
        return;
    }
    auto change = daemon::change_setting(file.config, key, value, modules_ui->known_modules());
    if (!change) {
        display().warning(change.error().message);
        return;
    }
    if (change->before == change->after) {
        display().info(std::format("{} vaut déjà {}.", change->key, change->after));
        return;
    }
    if (change->weakening) {
        display().warning(std::format("{} : {} → {} affaiblit la sécurité.", change->key,
                                      change->before, change->after));
        display().plain("  " + *change->weakening);
        display().plain("  " + paint("Tout comportement ayant un impact négatif causé par ce "
                                     "changement relève de votre seule responsabilité.",
                                     {.color = Color::Yellow}));
        const auto answer =
            display().ask(std::format("  Pour confirmer, tapez « {} » :", kAgreementPhrase));
        if (!answer || trim(*answer) != kAgreementPhrase) {
            display().info("Rien n'a été changé.");
            return;
        }
    }
    if (auto saved = file.save(change->updated); !saved) {
        display().error(core::describe(saved.error()));
        return;
    }
    file.config = change->updated;
    if (const auto audit = spdlog::get("audit")) {
        audit->info(
            "réglages : {} : {} → {} (/reglage{})", change->key, change->before, change->after,
            change->weakening ? std::format(", accord « {} »", kAgreementPhrase) : std::string{});
    }
    display().success(std::format("{} : {}", change->key, change->after));
    display().info("Pour l'appliquer dès maintenant : /redemarrer.");
}

/// Environnement des agents d'un plan, créé dans le fil de la requête : leur
/// affichage et leurs questions vont au client de cette requête.
agents::Environment Runtime::State::agent_environment() const {
    Ui& ui = display();
    agents::Environment environment;
    environment.model =
        [this](const agents::AgentSpec& member) -> core::Result<agents::ModelLease> {
        const llm::ModelChoice* choice =
            member.model ? switcher->find(*member.model) : switcher->active();
        if (choice == nullptr) {
            return core::make_error(core::ErrorCode::Config,
                                    member.model ? "modèle inconnu « " + *member.model + " »"
                                                 : std::string("aucun modèle actif"));
        }
        // Directive 17 : « local uniquement » vaut pour tous les agents ; un
        // modèle distant se confirme.
        if (!choice->local) {
            if (switcher->local_only()) {
                return core::make_error(core::ErrorCode::Config,
                                        "mode « local uniquement » : modèle distant « " +
                                            choice->name + " » refusé");
            }
            if (!display().ask_yes_no(std::format(
                    "L'agent « {} » utiliserait le modèle distant « {} » : sa tâche partirait "
                    "chez son fournisseur. Autoriser ? [o/N]",
                    member.name, choice->name))) {
                return core::make_error(core::ErrorCode::Config,
                                        "modèle distant « " + choice->name + " » refusé");
            }
        }
        auto provider = llm::make_provider(choice->config);
        if (!provider) {
            return std::unexpected(std::move(provider.error()));
        }
        return agents::ModelLease{
            .llm = std::move(*provider), .label = choice->name, .local = choice->local};
    };
    environment.tools = &agent_tools;
    environment.confirmer = [] { return std::make_unique<UiConfirmation>(); };
    environment.policy = file.config.permissions;
    environment.base_prompt = base_prompt;
    // Provenance : seules les demandes de l'utilisateur sont « demandées ».
    for (const auto& message : agent->history()) {
        if (const auto* user = std::get_if<core::UserMessage>(&message)) {
            environment.requester_text += user->content + "\n";
        }
    }
    environment.context = [&ui](const std::function<void()>& body) {
        const UiScope scope(ui);
        body();
    };
    environment.observer = [](const agents::AgentSpec& /*agent*/) {
        return core::HistoryObserver{
            .appended =
                [](const core::Message& message) {
                    if (const auto* reply = std::get_if<core::AssistantMessage>(&message)) {
                        display().tool_calls(reply->tool_calls);
                    } else if (const auto* result =
                                   std::get_if<core::ToolResultMessage>(&message)) {
                        display().tool_result(*result);
                    }
                },
            .removed_last = [] {},
        };
    };
    environment.on_start = [](const agents::AgentSpec& member, const agents::Task& task) {
        display().info(std::format("◇ {} ({}) : {}", member.name, member.role,
                                   terminal::ellipsize(task.instruction, 90)));
    };
    return environment;
}

/// /agents : l'équipe de l'utilisateur et les bornes de l'orchestrateur.
void Runtime::State::print_agents() const {
    const auto& orchestration = file.config.orchestration;
    display().plain(paint("Agents", {.color = Color::White, .bold = true}) +
                    paint(std::format("  orchestration {} · mode {} · {} tâches, {} agents créés, "
                                      "{} en parallèle au plus",
                                      orchestration.enabled ? "active" : "coupée",
                                      orchestration.mode, orchestration.max_tasks,
                                      orchestration.max_agents, orchestration.max_parallel),
                          {.color = Color::Gray}));
    if (file.config.agents.empty()) {
        display().info("Aucun agent configuré : Avenoir crée au besoin les agents d'une demande "
                       "(mode « both » ou « auto »). Pour composer votre équipe : /agent "
                       "ajouter <nom> <rôle>.");
    }
    for (const auto& member : file.config.agents) {
        std::string tools;
        for (const auto& tool : member.tools) {
            tools += (tools.empty() ? "" : ", ") + tool;
        }
        display().plain(std::format("  {} {}  {}", paint("◇", {.color = Color::Cyan}),
                                    paint(member.name, {.bold = true}), member.role));
        display().plain(paint(
            std::format("      modèle : {} · outils : {} · {} étapes{}",
                        member.model.value_or("modèle actif"), tools.empty() ? "aucun" : tools,
                        member.max_steps,
                        member.instructions.empty()
                            ? std::string{}
                            : " · consigne : " + terminal::ellipsize(member.instructions, 60)),
            {.color = Color::Gray}));
    }
    std::string allowed;
    for (const auto& spec : agent_tools.specs()) {
        allowed += (allowed.empty() ? "" : ", ") + spec.name;
    }
    display().plain(paint("Outils qu'un agent peut recevoir : " + allowed, {.color = Color::Gray}));
    display().plain(
        paint("Changer : /agent ajouter <nom> <rôle> · /agent outils <nom> <outil,…|aucun> "
              "· /agent modele <nom> <modèle|actif> · /agent consigne <nom> <texte> · "
              "/agent etapes <nom> <n> · /agent supprimer <nom>",
              {.color = Color::Gray}));
}

/// /agent … : compose l'équipe ; écrit par le sceau, noté à l'audit, appliqué
/// par /redemarrer. Un agent ne reçoit que des outils permis aux agents.
void Runtime::State::edit_agent(std::string_view argument) {
    const auto first = argument.find(' ');
    const std::string_view action = argument.substr(0, first);
    const std::string_view rest =
        first == std::string_view::npos ? std::string_view{} : trim(argument.substr(first + 1));
    const auto second = rest.find(' ');
    const std::string name(rest.substr(0, second));
    const std::string_view value =
        second == std::string_view::npos ? std::string_view{} : trim(rest.substr(second + 1));
    if (action.empty() || name.empty()) {
        display().warning("Usage : /agent <ajouter|outils|modele|consigne|etapes|supprimer> <nom> "
                          "[valeur] (voir /agents)");
        return;
    }
    core::AppConfig updated = file.config;
    auto& team = updated.agents;
    const auto found = std::ranges::find(team, name, &core::AgentProfile::name);
    if (action != "ajouter" && found == team.end()) {
        display().warning(std::format("Agent inconnu : « {} » (voir /agents).", name));
        return;
    }
    std::string summary;
    if (action == "ajouter") {
        if (found != team.end()) {
            display().warning(std::format("L'agent « {} » existe déjà.", name));
            return;
        }
        if (value.empty()) {
            display().warning("Usage : /agent ajouter <nom> <rôle>");
            return;
        }
        team.push_back(core::AgentProfile{.name = name,
                                          .role = std::string(value),
                                          .instructions = {},
                                          .model = std::nullopt,
                                          .tools = {},
                                          .max_steps = core::AgentProfile{}.max_steps});
        summary = std::format("agent « {} » ajouté ({}), sans outil", name, value);
    } else if (action == "supprimer") {
        team.erase(found);
        summary = std::format("agent « {} » supprimé", name);
    } else if (action == "outils") {
        std::vector<std::string> tools;
        if (value != "aucun") {
            for (const auto part : std::views::split(value, ',')) {
                const std::string tool(trim(std::string_view(part)));
                if (tool.empty()) {
                    continue;
                }
                // Moindre privilège : seulement des outils permis aux agents.
                if (!agent_tools.find(tool)) {
                    display().warning(std::format("Outil « {} » non permis aux agents (voir "
                                                  "/agents).",
                                                  tool));
                    return;
                }
                if (!std::ranges::contains(tools, tool)) {
                    tools.push_back(tool);
                }
            }
        }
        found->tools = std::move(tools);
        summary = std::format("outils de « {} » : {}", name, value);
    } else if (action == "modele" || action == "modèle") {
        if (value == "actif") {
            found->model.reset();
        } else {
            found->model = std::string(value);
        }
        summary = std::format("modèle de « {} » : {}", name, value);
    } else if (action == "consigne") {
        found->instructions = std::string(value);
        summary = std::format("consigne de « {} » changée", name);
    } else if (action == "etapes" || action == "étapes") {
        found->max_steps = 0;
        for (const char c : value) {
            if (c < '0' || c > '9' || found->max_steps > 100) {
                found->max_steps = 0;
                break;
            }
            found->max_steps = (found->max_steps * 10) + static_cast<std::size_t>(c - '0');
        }
        summary = std::format("étapes de « {} » : {}", name, value);
    } else {
        display().warning(std::format("Action inconnue : « {} » (voir /agents).", action));
        return;
    }
    // Mêmes règles que le fichier (nom, modèle existant, bornes).
    if (auto checked = core::parse_config(core::format_config(updated)); !checked) {
        display().warning(checked.error().message);
        return;
    }
    if (auto saved = file.save(updated); !saved) {
        display().error(core::describe(saved.error()));
        return;
    }
    file.config = std::move(updated);
    if (const auto audit = spdlog::get("audit")) {
        audit->info("équipe : {} (/agent)", summary);
    }
    display().success(summary + ".");
    display().info("Pour l'appliquer dès maintenant : /redemarrer.");
}

/// /demarrage [oui|non] : démarrage du démon à l'ouverture de session.
void Runtime::State::startup(std::string_view argument) {
    core::Result<AutostartState> state = autostart_state();
    if (argument == "oui" || argument == "non") {
        state = set_autostart(argument == "oui");
        if (state) {
            if (const auto audit = spdlog::get("audit")) {
                audit->info("démarrage à l'ouverture de session : {} ({})",
                            state->enabled ? "activé" : "retiré", state->where);
            }
        }
    } else if (!argument.empty()) {
        display().warning("Usage : /demarrage [oui|non]");
        return;
    }
    if (!state) {
        display().error(core::describe(state.error()));
        return;
    }
    display().info(state->enabled
                       ? std::format("Avenoir démarre à l'ouverture de session ({}). Pour "
                                     "l'empêcher : /demarrage non.",
                                     state->where)
                       : "Avenoir ne démarre pas à l'ouverture de session : il est lancé à la "
                         "demande. Pour l'activer : /demarrage oui.");
}

Outcome Runtime::State::command(std::string_view line, const std::stop_token& stop) {
    // « /commande argument libre »
    const auto space = line.find(' ');
    const std::string_view name = line.substr(0, space);
    const std::string_view argument =
        space == std::string_view::npos ? std::string_view{} : trim(line.substr(space + 1));

    if (name == "/quitter" || name == "/quit" || name == "/q") {
        return Outcome::Quit;
    }
    if (name == "/redemarrer") {
        return Outcome::Restart;
    }
    if (name == "/arreter" || name == "/arrêter") {
        return Outcome::Shutdown;
    }
    if (name == "/aide" || name == "/help") {
        print_help();
    } else if (name == "/nouvelle") {
        conversations->create(argument);
    } else if (name == "/reset") {
        if (const auto cleared = conversations->clear()) {
            display().clear();
            display().banner(model_label(*switcher), agent->tools().size(),
                             conversations->describe_current());
            display().success("Conversation effacée.");
            if (cleared->transcript) {
                display().info("Transcription : " + utf8_path(*cleared->transcript));
            }
        }
    } else if (name == "/agents") {
        print_agents();
    } else if (name == "/agent") {
        edit_agent(argument);
    } else if (name == "/demarrage" || name == "/démarrage") {
        startup(argument);
    } else if (name == "/configurer") {
        configure(stop);
    } else if (name == "/reglages") {
        print_settings();
    } else if (name == "/reglage") {
        change_setting(argument);
    } else if (name == "/conversations") {
        conversations->list();
    } else if (name == "/ouvrir") {
        conversations->open(argument);
    } else if (name == "/renommer") {
        conversations->rename(argument);
    } else if (name == "/supprimer") {
        conversations->remove(argument);
    } else if (name == "/historique") {
        conversations->show_history();
    } else if (name == "/transcription") {
        conversations->transcribe();
    } else if (name == "/outils" || name == "/tools") {
        print_tools();
    } else if (name == "/annuler") {
        if (const auto* journal = agent->last_undoable()) {
            offer_undo(*agent, *journal);
        } else {
            display().info("Rien à annuler.");
        }
    } else if (name == "/modele" || name == "/model") {
        if (argument.empty()) {
            print_models();
        } else if (agent->tools().find("switch_llm") == nullptr) {
            display().info("Un seul modèle configuré : rien à changer.");
        } else {
            // Comme un appel de la voie rapide : permissions, journal (/annuler
            // revient au modèle précédent) et confirmation pour un modèle distant.
            run_tool(line,
                     core::ToolCall{.id = {},
                                    .name = "switch_llm",
                                    .arguments = {{"name", std::string(argument)}}},
                     stop);
        }
    } else if (name == "/moteur") {
        if (argument.empty()) {
            std::string list;
            for (const auto& engine : engines->available()) {
                list += (list.empty() ? "" : ", ") + engine;
            }
            display().info("Recherche web : " + engines->label() + ". Moteurs : " + list + ".");
        } else {
            run_tool(line,
                     core::ToolCall{.id = {},
                                    .name = "set_search_engine",
                                    .arguments = {{"name", std::string(argument)}}},
                     stop);
        }
    } else if (name == "/charger" || name == "/decharger" || name == "/recharger") {
        const bool unload = name == "/decharger";
        const char* tool = unload ? "unload_llm" : "load_llm";
        if (agent->tools().find(tool) == nullptr) {
            display().info("Aucun modèle local à charger ou décharger.");
        } else {
            nlohmann::json arguments = nlohmann::json::object();
            if (!argument.empty()) {
                arguments["name"] = std::string(argument);
            }
            if (name == "/recharger") {
                arguments["reload"] = true;
            }
            std::string model = std::string(argument);
            if (model.empty()) {
                model = switcher->active() != nullptr ? switcher->active()->name : "?";
            }
            const char* verb = "Chargement";
            if (unload) {
                verb = "Déchargement";
            } else if (name == "/recharger") {
                verb = "Rechargement";
            }
            std::string label = std::format("{} du modèle « {} »", verb, model);
            run_tool(line,
                     core::ToolCall{.id = {}, .name = tool, .arguments = std::move(arguments)},
                     stop, [label = std::move(label)] { return label; });
        }
    } else if (name == "/memoire" || name == "/mémoire") {
        if (const auto text = switcher->status(argument)) {
            for (const auto& row : std::views::split(*text, '\n')) {
                display().plain("  " + std::string(std::string_view(row)));
            }
        } else {
            display().error(core::describe(text.error()));
        }
    } else if (name == "/raisonnement") {
        if (argument.empty()) {
            if (const auto* active = switcher->active()) {
                display().info(
                    std::format("Raisonnement de « {} » : {}.", active->name,
                                llm::reasoning_label(llm::ModelSwitcher::reasoning(*active))));
            } else {
                display().info("Aucun modèle.");
            }
        } else if (agent->tools().find("set_reasoning") == nullptr) {
            display().info("Aucun modèle dont le raisonnement se règle (openai_compatible).");
        } else {
            // « /raisonnement élevé » ou « /raisonnement élevé leger ».
            const auto separator = argument.find(' ');
            nlohmann::json arguments{{"level", std::string(argument.substr(0, separator))}};
            if (separator != std::string_view::npos) {
                arguments["name"] = std::string(trim(argument.substr(separator + 1)));
            }
            run_tool(line,
                     core::ToolCall{
                         .id = {}, .name = "set_reasoning", .arguments = std::move(arguments)},
                     stop);
        }
    } else if (name == "/modules") {
        modules_ui->print_status();
    } else if (name == "/risque") {
        modules_ui->risk(argument, conversations->store());
    } else if (const auto* own = modules_ui->find_command(name.substr(1))) {
        // Commande d'un module : l'outil est appelé sans LLM, sous contrôle
        // des permissions, comme par la voie rapide.
        run_tool(line, ModulesUi::make_call(*own, argument), stop);
    } else {
        display().warning(std::format("Commande inconnue : {} (voir /aide)", name));
    }
    return Outcome::Continue;
}

} // namespace avenoir::daemon
