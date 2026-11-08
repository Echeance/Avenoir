#include "avenoir/llm/models.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/llm/factory.hpp"

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
                             ProviderFactory factory)
    : models_(std::move(models)), confirmer_(confirmer), factory_(std::move(factory)) {
    if (!factory_) {
        factory_ = [](const core::LlmConfig& config) { return make_provider(config); };
    }
}

core::Result<std::unique_ptr<core::LlmProvider>>
ModelSwitcher::start(std::optional<std::string_view> preferred) {
    if (models_.empty()) {
        active_ = 0;
        return std::unique_ptr<core::LlmProvider>{};
    }
    const auto* chosen = preferred ? find(*preferred) : nullptr;
    if (chosen == nullptr) {
        chosen = models_.data();
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
    active_ = static_cast<std::size_t>(target - models_.data());
    if (on_switch_) {
        on_switch_(*target);
    }
    return std::format("Modèle actif : {}.", describe(*target));
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
