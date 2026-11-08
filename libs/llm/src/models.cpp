#include "avenoir/llm/models.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/llm/factory.hpp"

#include "http.hpp"

namespace avenoir::llm {

namespace {

constexpr std::string_view kToolName = "switch_llm";

/// Hôte d'une URL (« http://user@hôte:port/chemin » → « hôte »), en minuscules.
std::string url_host(std::string_view url) {
    if (const auto scheme = url.find("://"); scheme != std::string_view::npos) {
        url.remove_prefix(scheme + 3);
    }
    url = url.substr(0, url.find_first_of("/?#"));
    if (const auto at = url.rfind('@'); at != std::string_view::npos) {
        url.remove_prefix(at + 1);
    }
    std::string host;
    if (url.starts_with('[')) {
        host = std::string(url.substr(1, url.find(']') - 1)); // IPv6 : « [::1]:11434 »
    } else {
        host = std::string(url.substr(0, url.find(':')));
    }
    std::ranges::transform(host, host.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return host;
}

/// Racine du serveur d'un modèle : « http://localhost:11434 » pour
/// « http://LocalHost:11434/v1/ » (sans « /v1 », hôte en minuscules).
std::string server_root(const core::OpenAiCompatibleConfig& config) {
    std::string root = config.base_url;
    while (root.ends_with('/')) {
        root.pop_back();
    }
    if (root.ends_with("/v1")) {
        root.resize(root.size() - 3);
    }
    const auto scheme = root.find("://");
    const std::size_t host_start = scheme == std::string::npos ? 0 : scheme + 3;
    const std::size_t host_end = std::min(root.find('/', host_start), root.size());
    for (std::size_t i = host_start; i < host_end; ++i) {
        if (root[i] >= 'A' && root[i] <= 'Z') {
            root[i] = static_cast<char>(root[i] - 'A' + 'a');
        }
    }
    return root;
}

bool is_loopback(std::string_view host) {
    return host == "localhost" || host == "::1" || host.starts_with("127.");
}

/// Minuscules sans accents (lettres latines courantes du français), pour
/// comparer un nom dit ou tapé à celui de la configuration.
std::string fold(std::string_view text) {
    static constexpr std::array<std::pair<std::string_view, char>, 16> kAccents{{
        {"é", 'e'},
        {"è", 'e'},
        {"ê", 'e'},
        {"ë", 'e'},
        {"à", 'a'},
        {"â", 'a'},
        {"î", 'i'},
        {"ï", 'i'},
        {"ô", 'o'},
        {"ù", 'u'},
        {"û", 'u'},
        {"ü", 'u'},
        {"ç", 'c'},
        {"É", 'e'},
        {"È", 'e'},
        {"À", 'a'},
    }};
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        // Boucle explicite : l'itérateur d'un std::array n'est un pointeur
        // qu'avec certaines bibliothèques standard.
        bool replaced = false;
        for (const auto& [accent, letter] : kAccents) {
            if (text.substr(i).starts_with(accent)) {
                out += letter;
                i += accent.size();
                replaced = true;
                break;
            }
        }
        if (replaced) {
            continue;
        }
        const char c = text[i++];
        out += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

std::string provider_label(const core::LlmConfig& config) {
    return std::visit(
        [](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, core::AnthropicConfig>) {
                return "API Anthropic";
            } else if constexpr (std::is_same_v<T, core::ClaudeCodeConfig>) {
                return "Claude Code";
            } else {
                return {};
            }
        },
        config);
}

struct SwitchArgs {
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SwitchArgs, name)

class SwitchLlmTool final : public core::Tool {
  public:
    explicit SwitchLlmTool(ModelSwitcher& switcher) : switcher_(&switcher) {
        std::string list;
        for (const auto& model : switcher.models()) {
            list += (list.empty() ? "" : " ; ") + describe(model);
        }
        description_ = "Change le modèle de langage (LLM) qui fait fonctionner l'assistant, y "
                       "compris pour revenir à un modèle déjà utilisé. La conversation continue "
                       "avec le nouveau modèle. Noms acceptés : " +
                       switcher.names() + ". Détail (nom : modèle, local ou distant) : " + list +
                       ".";
    }

    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = std::string(kToolName),
            .description = description_,
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"name",
                                {{"type", "string"},
                                 {"description", "Nom du modèle, tel que dans la liste"}}}}},
                             {"required", nlohmann::json::array({"name"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto args = core::parse_args<SwitchArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        const std::string previous =
            switcher_->active() != nullptr ? switcher_->active()->name : "";
        const auto* target = switcher_->find(args->name);
        if (target != nullptr && target->name == previous) {
            return core::ToolOutput{.content =
                                        std::format("Le modèle « {} » est déjà actif.", previous)};
        }
        auto switched = switcher_->switch_to(args->name);
        if (!switched) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                   .message = switched.error().message});
        }
        const std::string now = switcher_->active()->name; // changement réussi : non nul
        ModelSwitcher* switcher = switcher_;
        return core::ToolOutput{
            .content = std::move(*switched),
            .undo =
                core::UndoAction{
                    .description = std::format("revenir au modèle « {} »", previous),
                    .run = [switcher, previous, now] -> core::ToolResult {
                        // L'état ne doit pas avoir changé depuis : sinon, on
                        // n'écrase pas le choix fait entre-temps.
                        if (switcher->active() == nullptr || switcher->active()->name != now) {
                            return std::unexpected(core::ToolError{
                                .kind = core::ToolErrorKind::Execution,
                                .message = std::format("le modèle actif n'est plus « {} »", now)});
                        }
                        auto back = switcher->switch_to(previous);
                        if (!back) {
                            return std::unexpected(
                                core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                .message = back.error().message});
                        }
                        return core::ToolOutput{.content = std::move(*back)};
                    },
                },
        };
    }

  private:
    ModelSwitcher* switcher_;
    std::string description_;
};

} // namespace

bool is_local(const core::LlmConfig& config, std::optional<bool> declared) {
    const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&config);
    if (openai == nullptr) {
        return false; // API Anthropic, Claude Code : toujours distants.
    }
    return declared.value_or(is_loopback(url_host(openai->base_url)));
}

std::string model_id(const core::LlmConfig& config) {
    return std::visit(
        [](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, core::ClaudeCodeConfig>) {
                return c.model.value_or("modèle par défaut");
            } else {
                return c.model;
            }
        },
        config);
}

std::string describe(const ModelChoice& choice) {
    const std::string provider = provider_label(choice.config);
    return std::format("{} : {}{}, {}", choice.name, model_id(choice.config),
                       provider.empty() ? "" : " (" + provider + ")",
                       choice.local ? "local" : "distant");
}

std::vector<ModelChoice> models_from(const core::AppConfig& config) {
    std::vector<ModelChoice> models;
    models.reserve(config.models.size());
    for (const auto& model : config.models) {
        models.push_back(ModelChoice{.name = model.name,
                                     .config = model.config,
                                     .local = is_local(model.config, model.local)});
    }
    return models;
}

ModelSwitcher::ModelSwitcher(std::vector<ModelChoice> models, core::ConfirmationHandler* confirmer,
                             ProviderFactory factory, JsonPoster poster, JsonGetter getter)
    : models_(std::move(models)), confirmer_(confirmer), factory_(std::move(factory)),
      poster_(std::move(poster)), getter_(std::move(getter)) {
    if (!getter_) {
        getter_ = [](const std::string& url) {
            // Question rapide à un serveur local : ne jamais faire attendre.
            return detail::get_json(url, std::chrono::milliseconds{500});
        };
    }
    if (!factory_) {
        factory_ = [](const core::LlmConfig& config) { return make_provider(config); };
    }
    if (!poster_) {
        poster_ = [](const std::string& url, const nlohmann::json& body,
                     const std::stop_token& stop) {
            // Charger un gros modèle prend du temps (~20 s pour 20 Go).
            return detail::post_json(detail::HttpRequest{.url = url,
                                                         .headers = {},
                                                         .body = body.dump(),
                                                         .timeout = std::chrono::seconds{180}},
                                     stop);
        };
    }
}

core::Result<void> ModelSwitcher::send_residency(const ModelChoice& model, bool load,
                                                 const std::stop_token& stop) const {
    const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&model.config);
    if (openai == nullptr || !model.local) {
        return core::make_error(core::ErrorCode::Config,
                                std::format("« {} » est un modèle distant : rien à charger ni à "
                                            "décharger sur cette machine",
                                            model.name));
    }
    // API d'Ollama, à côté de son API compatible OpenAI (…/v1) : une requête
    // sans prompt charge le modèle ; keep_alive = 0 le décharge.
    const std::string root = server_root(*openai);
    nlohmann::json body{{"model", openai->model}};
    if (!load) {
        body["keep_alive"] = 0;
    }
    auto sent = poster_(root + "/api/generate", body, stop);
    if (!sent) {
        if (sent.error().code == core::ErrorCode::Cancelled) {
            return std::unexpected(std::move(sent.error()));
        }
        return core::make_error(core::ErrorCode::Llm,
                                std::format("le serveur de « {} » n'a pas pu {} le modèle "
                                            "(Ollama seulement) : {}",
                                            model.name, load ? "charger" : "décharger",
                                            sent.error().message));
    }
    return {};
}

core::Result<std::string> ModelSwitcher::set_residency(std::string_view name, Residency residency,
                                                       const std::stop_token& stop) {
    const ModelChoice* target = name.empty() ? active() : find(name);
    if (target == nullptr) {
        return core::make_error(
            core::ErrorCode::Config,
            name.empty() ? std::string("aucun modèle actif")
                         : std::format("modèle inconnu « {} » (modèles : {})", name, names()));
    }
    const auto index = static_cast<std::size_t>(target - models_.data());
    if (residency == Residency::Unload && index == active_ && agent_ != nullptr) {
        // Vérifie d'abord que c'est possible (modèle local), puis attend la
        // fin de la requête : la réponse en cours rechargerait le modèle.
        if (!std::holds_alternative<core::OpenAiCompatibleConfig>(target->config) ||
            !target->local) {
            return core::make_error(core::ErrorCode::Config,
                                    std::format("« {} » est un modèle distant : rien à décharger "
                                                "sur cette machine",
                                                target->name));
        }
        defer_unload(index);
        return std::format("Le modèle « {} » sera déchargé à la fin de cette réponse ; il se "
                           "rechargera à la prochaine requête.",
                           target->name);
    }
    if (residency != Residency::Load) {
        if (auto unloaded = send_residency(*target, false, stop); !unloaded) {
            return std::unexpected(std::move(unloaded.error()));
        }
        cancel_unload(index);
    }
    if (residency != Residency::Unload) {
        if (auto loaded = send_residency(*target, true, stop); !loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
        cancel_unload(index); // chargé à nouveau : plus rien à décharger
    }
    switch (residency) {
    case Residency::Load:
        return std::format("Modèle « {} » chargé en mémoire.", target->name);
    case Residency::Unload:
        return std::format("Modèle « {} » déchargé : sa mémoire est libérée.", target->name);
    case Residency::Reload:
        break;
    }
    return std::format("Modèle « {} » rechargé.", target->name);
}

namespace {

/// Niveau dit ou tapé → valeur de l'API ; « défaut » → nullopt ; inconnu → erreur.
core::Result<std::optional<std::string>> parse_reasoning(std::string_view level) {
    const std::string folded = fold(level);
    for (const auto& [words, value] :
         std::to_array<std::pair<std::array<std::string_view, 4>, std::string_view>>({
             {{"none", "aucun", "desactive", "off"}, "none"},
             {{"low", "faible", "bas", "leger"}, "low"},
             {{"medium", "moyen", "normal", "active"}, "medium"},
             {{"high", "eleve", "haut", "fort"}, "high"},
         })) {
        if (std::ranges::contains(words, folded)) {
            return std::optional<std::string>(value);
        }
    }
    if (folded == "defaut" || folded == "default" || folded == "par-defaut") {
        return std::optional<std::string>{};
    }
    return core::make_error(core::ErrorCode::Config,
                            std::format("niveau de raisonnement inconnu « {} » (aucun, faible, "
                                        "moyen, élevé ou défaut)",
                                        level));
}

} // namespace

std::string reasoning_label(const std::optional<std::string>& effort) {
    if (!effort) {
        return "par défaut (choix du serveur)";
    }
    if (*effort == "none") {
        return "aucun";
    }
    if (*effort == "low") {
        return "faible";
    }
    if (*effort == "medium") {
        return "moyen";
    }
    if (*effort == "high") {
        return "élevé";
    }
    return *effort;
}

std::optional<std::string> ModelSwitcher::reasoning(const ModelChoice& model) {
    const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&model.config);
    return openai != nullptr ? openai->reasoning_effort : std::nullopt;
}

void ModelSwitcher::restore_reasoning(std::string_view name, std::string_view level) {
    const auto* found = find(name);
    auto effort = parse_reasoning(level.empty() ? "défaut" : level);
    if (found == nullptr || !effort) {
        return;
    }
    auto& model = models_[static_cast<std::size_t>(found - models_.data())];
    if (auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&model.config)) {
        openai->reasoning_effort = *effort;
    }
}

core::Result<std::string> ModelSwitcher::set_reasoning(std::string_view name,
                                                       std::string_view level) {
    const ModelChoice* target = name.empty() ? active() : find(name);
    if (target == nullptr) {
        return core::make_error(
            core::ErrorCode::Config,
            name.empty() ? std::string("aucun modèle actif")
                         : std::format("modèle inconnu « {} » (modèles : {})", name, names()));
    }
    auto effort = parse_reasoning(level);
    if (!effort) {
        return std::unexpected(std::move(effort.error()));
    }
    const auto index = static_cast<std::size_t>(target - models_.data());
    auto updated = models_[index].config;
    auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&updated);
    if (openai == nullptr) {
        return core::make_error(
            core::ErrorCode::Config,
            std::format("le raisonnement de « {} » ne se règle pas (openai_compatible seulement)",
                        target->name));
    }
    openai->reasoning_effort = *effort;
    if (index == active_ && agent_ != nullptr) {
        // Nouveau fournisseur, même modèle : le serveur le garde en mémoire.
        auto provider = factory_(updated);
        if (!provider) {
            return std::unexpected(std::move(provider.error()));
        }
        agent_->set_llm(std::move(*provider));
    }
    models_[index].config = std::move(updated);
    if (on_reasoning_) {
        on_reasoning_(models_[index]);
    }
    return std::format("Raisonnement de « {} » : {}.", models_[index].name,
                       reasoning_label(*effort));
}

std::optional<LoadState> ModelSwitcher::load_state(const ModelChoice& model) const {
    const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&model.config);
    if (openai == nullptr || !model.local) {
        return std::nullopt;
    }
    auto listed = getter_(server_root(*openai) + "/api/ps");
    if (!listed || !listed->is_object() || !listed->contains("models") ||
        !(*listed)["models"].is_array()) {
        return std::nullopt; // pas Ollama, ou injoignable : on ne sait pas
    }
    // Ollama nomme « modèle:latest » un modèle donné sans étiquette.
    const std::string wanted = openai->model;
    const std::string tagged = wanted.contains(':') ? wanted : wanted + ":latest";
    for (const auto& entry : (*listed)["models"]) {
        for (const char* key : {"name", "model"}) {
            if (!entry.contains(key) || !entry[key].is_string()) {
                continue;
            }
            const auto name = entry[key].get<std::string>();
            if (name != wanted && name != tagged) {
                continue;
            }
            LoadState state{.loaded = true, .vram_bytes = std::nullopt};
            if (entry.contains("size_vram") && entry["size_vram"].is_number_unsigned()) {
                state.vram_bytes = entry["size_vram"].get<std::uint64_t>();
            }
            return state;
        }
    }
    return LoadState{.loaded = false, .vram_bytes = std::nullopt};
}

std::optional<bool> ModelSwitcher::is_loaded(const ModelChoice& model) const {
    const auto state = load_state(model);
    return state ? std::optional<bool>(state->loaded) : std::nullopt;
}

core::Result<std::string> ModelSwitcher::status(std::string_view name) const {
    std::vector<const ModelChoice*> wanted;
    if (name.empty()) {
        for (const auto& model : models_) {
            wanted.push_back(&model);
        }
    } else if (const auto* model = find(name)) {
        wanted.push_back(model);
    } else {
        return core::make_error(core::ErrorCode::Config,
                                std::format("modèle inconnu « {} » (modèles : {})", name, names()));
    }
    if (wanted.empty()) {
        return std::string("Aucun modèle configuré.");
    }
    std::string out;
    for (const auto* model : wanted) {
        std::string line = model->name + " : ";
        if (!model->local) {
            line += "distant (rien à charger sur cette machine)";
        } else if (const auto state = load_state(*model); !state) {
            line += "état inconnu (serveur injoignable, ou autre qu'Ollama)";
        } else if (!state->loaded) {
            line += "pas chargé";
        } else if (state->vram_bytes) {
            line += std::format("chargé ({:.1f} Go de mémoire vidéo)",
                                static_cast<double>(*state->vram_bytes) / 1e9);
        } else {
            line += "chargé";
        }
        if (model == active()) {
            line += ", actif";
        }
        out += (out.empty() ? "" : "\n") + line + ".";
    }
    // Virgule décimale, à la française.
    for (std::size_t dot = out.find(" Go"); dot != std::string::npos;
         dot = out.find(" Go", dot + 3)) {
        const auto point = out.rfind('.', dot);
        if (point != std::string::npos && dot - point == 2) {
            out[point] = ',';
        }
    }
    return out;
}

void ModelSwitcher::defer_unload(std::size_t index) {
    if (!std::ranges::contains(pending_unload_, index)) {
        pending_unload_.push_back(index);
    }
}

void ModelSwitcher::cancel_unload(std::size_t index) {
    std::erase(pending_unload_, index);
}

std::optional<std::string> ModelSwitcher::finish_request() {
    std::string notes;
    for (const std::size_t index : std::exchange(pending_unload_, {})) {
        if (index >= models_.size()) {
            continue;
        }
        const ModelChoice& model = models_[index];
        std::string note;
        if (auto unloaded = send_residency(model, false, {}); !unloaded) {
            note = std::format("Déchargement de « {} » impossible : {}", model.name,
                               unloaded.error().message);
        } else {
            note = std::format("Modèle « {} » déchargé : sa mémoire est libérée.", model.name);
        }
        notes += (notes.empty() ? "" : " ") + note;
    }
    if (notes.empty()) {
        return std::nullopt;
    }
    return notes;
}

core::Result<std::unique_ptr<core::LlmProvider>>
ModelSwitcher::start(std::optional<std::string_view> preferred) {
    if (models_.empty()) {
        active_ = 0;
        return std::unique_ptr<core::LlmProvider>{};
    }
    const auto allowed = [this](const ModelChoice* model) {
        return model != nullptr && (!local_only_ || model->local);
    };
    const auto* chosen = preferred ? find(*preferred) : nullptr;
    if (!allowed(chosen)) {
        chosen = nullptr;
        for (const auto& model : models_) {
            if (allowed(&model)) {
                chosen = &model;
                break;
            }
        }
    }
    if (chosen == nullptr) {
        active_ = models_.size(); // aucun modèle permis : voie rapide seulement
        return std::unique_ptr<core::LlmProvider>{};
    }
    auto provider = factory_(chosen->config);
    if (!provider) {
        return std::unexpected(std::move(provider.error()));
    }
    active_ = static_cast<std::size_t>(chosen - models_.data());
    return std::move(*provider);
}

const ModelChoice* ModelSwitcher::find(std::string_view name) const {
    const std::string wanted = fold(name);
    for (const auto& model : models_) {
        if (fold(model.name) == wanted || fold(model_id(model.config)) == wanted) {
            return &model;
        }
    }
    return nullptr;
}

core::Result<std::string> ModelSwitcher::switch_to(std::string_view name) {
    const auto* target = find(name);
    if (target == nullptr) {
        return core::make_error(core::ErrorCode::Config,
                                std::format("modèle inconnu « {} » (modèles : {})", name, names()));
    }
    if (agent_ == nullptr) {
        return core::make_error(core::ErrorCode::Config, "aucun agent auquel donner le modèle");
    }
    if (!target->local && local_only_) {
        return core::make_error(core::ErrorCode::Config,
                                std::format("mode « local uniquement » : le modèle distant « {} » "
                                            "n'est pas utilisable",
                                            target->name));
    }
    if (!target->local) {
        // Directive n° 17 : la conversation sortirait de la machine.
        const core::ToolSpec spec{
            .name = std::string(kToolName),
            .description = "Passer à un modèle distant : la conversation lui sera envoyée.",
            .input_schema = nlohmann::json::object(),
            .risk = core::RiskLevel::Sensitive,
        };
        const core::ToolCall call{
            .id = "switch", .name = std::string(kToolName), .arguments = {{"name", target->name}}};
        if (confirmer_ == nullptr || confirmer_->confirm(spec, call) != core::Decision::Approved) {
            return core::make_error(
                core::ErrorCode::Config,
                std::format("passage au modèle distant « {} » refusé", target->name));
        }
    }
    auto provider = factory_(target->config);
    if (!provider) {
        return std::unexpected(std::move(provider.error()));
    }
    agent_->set_llm(std::move(*provider));
    const std::size_t previous = active_;
    active_ = static_cast<std::size_t>(target - models_.data());
    cancel_unload(active_); // il sert de nouveau : surtout ne pas le décharger
    if (on_switch_) {
        on_switch_(*target);
    }
    std::string message = std::format("Modèle actif : {}.", describe(*target));
    // L'ancien modèle n'est déchargé que s'il partage le serveur du nouveau :
    // c'est là seulement que sa mémoire servira au nouveau.
    if (unload_on_switch_ && previous < models_.size() && previous != active_) {
        const ModelChoice& old = models_[previous];
        const auto* old_server = std::get_if<core::OpenAiCompatibleConfig>(&old.config);
        const auto* new_server = std::get_if<core::OpenAiCompatibleConfig>(&target->config);
        if (old.local && old_server != nullptr && new_server != nullptr &&
            server_root(*old_server) == server_root(*new_server) &&
            old_server->model != new_server->model) {
            defer_unload(previous);
            message += std::format(" L'ancien modèle « {} » sera déchargé.", old.name);
        }
    }
    return message;
}

std::string ModelSwitcher::names() const {
    std::string out;
    for (const auto& model : models_) {
        out += (out.empty() ? "" : ", ") + model.name;
    }
    return out;
}

std::shared_ptr<core::Tool> make_switch_llm_tool(ModelSwitcher& switcher) {
    return std::make_shared<SwitchLlmTool>(switcher);
}

namespace {

struct ResidencyArgs {
    std::string name;
    bool reload = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ResidencyArgs, name, reload)

class ResidencyTool final : public core::Tool {
  public:
    ResidencyTool(ModelSwitcher& switcher, bool load) : switcher_(&switcher), load_(load) {}

    [[nodiscard]] core::ToolSpec spec() const override {
        nlohmann::json properties{{"name",
                                   {{"type", "string"},
                                    {"description", "Nom du modèle (" + switcher_->names() +
                                                        ") ; absent : le modèle "
                                                        "actif"}}}};
        if (load_) {
            properties["reload"] = {
                {"type", "boolean"},
                {"description", "true : le décharger d'abord, puis le recharger"}};
        }
        return core::ToolSpec{
            .name = load_ ? "load_llm" : "unload_llm",
            .description =
                load_ ? "Charge (« allume ») un modèle local en mémoire pour qu'il réponde tout de "
                        "suite, ou le recharge. Modèles locaux servis par Ollama seulement."
                      : "Décharge (« stoppe ») un modèle local de la mémoire, ce qui libère la "
                        "mémoire vidéo (par exemple avant un jeu). Il se rechargera tout seul à "
                        "la requête suivante. Modèles locaux servis par Ollama seulement.",
            .input_schema = {{"type", "object"}, {"properties", std::move(properties)}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& stop) const override {
        auto args = core::parse_args<ResidencyArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        Residency residency = Residency::Unload;
        if (load_) {
            residency = args->reload ? Residency::Reload : Residency::Load;
        }
        auto done = switcher_->set_residency(args->name, residency, stop);
        if (!done) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                   .message = done.error().message});
        }
        return core::ToolOutput{.content = std::move(*done)};
    }

  private:
    ModelSwitcher* switcher_;
    bool load_;
};

} // namespace

std::vector<std::shared_ptr<core::Tool>> make_residency_tools(ModelSwitcher& switcher) {
    return {std::make_shared<ResidencyTool>(switcher, /*load=*/false),
            std::make_shared<ResidencyTool>(switcher, /*load=*/true)};
}

core::Result<void> register_residency_rules(core::FastRouter& router) {
    const nlohmann::json none = nlohmann::json::object();
    const nlohmann::json reload{{"reload", true}};
    // Le mot « modèle » (ou « llm ») est exigé, sauf pour « libère la carte graphique ».
    constexpr auto kUnload = R"(^(?:décharge|stoppe|arrête|éteins)\s+(?:le\s+)?(?:modèle|llm))";
    constexpr auto kLoad = R"(^(?:charge|allume|démarre|lance)\s+(?:le\s+)?(?:modèle|llm))";
    constexpr auto kReload = R"(^recharge\s+(?:le\s+)?(?:modèle|llm))";
    constexpr auto kNamed = R"(\s+(\S+)$)";
    for (const auto& [pattern, tool, arg, fixed] :
         std::to_array<std::tuple<std::string, std::string, std::string, nlohmann::json>>({
             {std::string(kUnload) + kNamed, "unload_llm", "name", none},
             {std::string(kUnload) + "$", "unload_llm", "", none},
             {R"(^libère\s+la\s+(?:carte\s+graphique|mémoire\s+vidéo|vram)$)", "unload_llm", "",
              none},
             {std::string(kLoad) + kNamed, "load_llm", "name", none},
             {std::string(kLoad) + "$", "load_llm", "", none},
             {std::string(kReload) + kNamed, "load_llm", "name", reload},
             {std::string(kReload) + "$", "load_llm", "", reload},
         })) {
        if (auto added = router.add_rule(pattern, tool, arg, fixed); !added) {
            return added;
        }
    }
    return {};
}

namespace {

struct ReasoningArgs {
    std::string level;
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ReasoningArgs, level, name)

class ReasoningTool final : public core::Tool {
  public:
    explicit ReasoningTool(ModelSwitcher& switcher) : switcher_(&switcher) {}

    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "set_reasoning",
            .description = "Règle l'effort de raisonnement (réflexion avant de répondre) d'un "
                           "modèle, sans le recharger : « aucun » est le plus rapide, « élevé » le "
                           "plus réfléchi mais plus lent. Modèles openai_compatible seulement.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"level",
                                {{"type", "string"},
                                 {"enum", {"aucun", "faible", "moyen", "élevé", "défaut"}},
                                 {"description", "Niveau de raisonnement"}}},
                               {"name",
                                {{"type", "string"},
                                 {"description", "Nom du modèle (" + switcher_->names() +
                                                     ") ; absent : le modèle actif"}}}}},
                             {"required", nlohmann::json::array({"level"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto args = core::parse_args<ReasoningArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        const ModelChoice* target =
            args->name.empty() ? switcher_->active() : switcher_->find(args->name);
        const std::string model = target != nullptr ? target->name : args->name;
        const auto previous = target != nullptr ? ModelSwitcher::reasoning(*target) : std::nullopt;
        auto done = switcher_->set_reasoning(args->name, args->level);
        if (!done) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                   .message = done.error().message});
        }
        const auto now = ModelSwitcher::reasoning(*switcher_->find(model));
        ModelSwitcher* switcher = switcher_;
        return core::ToolOutput{
            .content = std::move(*done),
            .undo =
                core::UndoAction{
                    .description = std::format("remettre le raisonnement de « {} » à {}", model,
                                               reasoning_label(previous)),
                    .run = [switcher, model, previous, now] -> core::ToolResult {
                        const auto* current = switcher->find(model);
                        if (current == nullptr || ModelSwitcher::reasoning(*current) != now) {
                            return std::unexpected(
                                core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                .message = "le raisonnement a changé depuis"});
                        }
                        auto back = switcher->set_reasoning(model, previous.value_or("défaut"));
                        if (!back) {
                            return std::unexpected(
                                core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                .message = back.error().message});
                        }
                        return core::ToolOutput{.content = std::move(*back)};
                    },
                },
        };
    }

  private:
    ModelSwitcher* switcher_;
};

} // namespace

namespace {

struct StatusArgs {
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StatusArgs, name)

class StatusTool final : public core::Tool {
  public:
    explicit StatusTool(const ModelSwitcher& switcher) : switcher_(&switcher) {}

    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "llm_status",
            .description = "Indique si les modèles (LLM) sont chargés en mémoire, et la mémoire "
                           "vidéo qu'ils occupent, ainsi que le modèle actif.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"name",
                                {{"type", "string"},
                                 {"description", "Nom du modèle (" + switcher_->names() +
                                                     ") ; absent : tous"}}}}}},
            .risk = core::RiskLevel::ReadOnly,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto args = core::parse_args<StatusArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        auto text = switcher_->status(args->name);
        if (!text) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                   .message = text.error().message});
        }
        return core::ToolOutput{.content = std::move(*text)};
    }

  private:
    const ModelSwitcher* switcher_;
};

} // namespace

std::shared_ptr<core::Tool> make_status_tool(const ModelSwitcher& switcher) {
    return std::make_shared<StatusTool>(switcher);
}

core::Result<void> register_status_rules(core::FastRouter& router) {
    const nlohmann::json none = nlohmann::json::object();
    for (
        const auto& [pattern, arg] : std::to_array<std::pair<std::string, std::string>>({
            {R"(^(?:quels?\s+(?:sont\s+les\s+)?modèles?\s+(?:sont|est)\s+(?:chargés?|en\s+mémoire)|état\s+des\s+modèles)\s*\??$)",
             ""},
            {R"(^(?:est-ce\s+que\s+)?le\s+modèle\s+(\S+)\s+est(?:-il)?\s+chargé\s*\??$)", "name"},
        })) {
        if (auto added = router.add_rule(pattern, "llm_status", arg, none); !added) {
            return added;
        }
    }
    return {};
}

std::shared_ptr<core::Tool> make_reasoning_tool(ModelSwitcher& switcher) {
    return std::make_shared<ReasoningTool>(switcher);
}

core::Result<void> register_reasoning_rules(core::FastRouter& router) {
    // Le mot « raisonnement » est exigé.
    const nlohmann::json none = nlohmann::json::object();
    for (const auto& [pattern, arg, fixed] :
         std::to_array<std::tuple<std::string, std::string, nlohmann::json>>({
             {R"(^(?:mets|passe|règle)\s+(?:le\s+)?raisonnement\s+(?:à|sur|en)\s+(\S+)$)", "level",
              none},
             {R"(^raisonnement\s+(\S+)$)", "level", none},
             {R"(^(?:désactive|coupe|arrête)\s+(?:le\s+)?raisonnement$)", "",
              nlohmann::json{{"level", "aucun"}}},
             {R"(^(?:active|allume)\s+(?:le\s+)?raisonnement$)", "",
              nlohmann::json{{"level", "moyen"}}},
         })) {
        if (auto added = router.add_rule(pattern, "set_reasoning", arg, fixed); !added) {
            return added;
        }
    }
    return {};
}

core::Result<void> register_switch_rules(core::FastRouter& router) {
    // Le mot « modèle » (ou « llm ») est exigé : « passe à la suite » part au LLM.
    for (
        const auto* pattern : {
            R"(^(?:passe|bascule|change)\s+(?:sur|à|vers|pour)\s+(?:le\s+)?(?:modèle|llm)\s+(\S+)$)",
            R"(^utilise\s+(?:le\s+)?(?:modèle|llm)\s+(\S+)$)",
        }) {
        if (auto added = router.add_rule(pattern, std::string(kToolName), "name"); !added) {
            return added;
        }
    }
    return {};
}

} // namespace avenoir::llm
