#include "avenoir/llm/store.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "http.hpp"

namespace avenoir::llm {

namespace {

// Banc d'essai (evals/resultats/COMPARAISON.md) : séries 10 à 12, mémoire
// vidéo mesurée par `ollama ps` (RX 7900 XTX), place sur le disque par
// `ollama list`.
constexpr std::array<CatalogModel, 3> kCatalog{{
    {.name = "gemma4",
     .tag = "gemma4:26b-a4b",
     .label = "Gemma 4 26B-A4B",
     .note = "conseillé : le plus sûr au quotidien et rapide",
     .score = "135/135 (série 12)",
     .video_memory_gb = 21,
     .disk_gb = 18},
    {.name = "qwen3-8",
     .tag = "qwen3.8:27b-q4_K_M",
     .label = "Qwen3.8 27B",
     .note = "pour le code et la réflexion, plus lent",
     .score = "131/132 (série 10)",
     .video_memory_gb = 17,
     .disk_gb = 17},
    {.name = "ministral-3",
     .tag = "ministral-3:8b",
     .label = "Ministral 3 8B",
     .note = "léger, pour une petite carte graphique (suit plus souvent une injection)",
     .score = "122/135 (série 12)",
     .video_memory_gb = 11,
     .disk_gb = 6},
}};

constexpr std::string_view kOllamaPort = ":11434";

} // namespace

std::span<const CatalogModel> model_catalog() {
    return kCatalog;
}

const CatalogModel* find_catalog_model(std::string_view name) {
    for (const auto& model : kCatalog) {
        if (model.name == name || model.tag == name) {
            return &model;
        }
    }
    return nullptr;
}

bool is_valid_model_tag(std::string_view tag) {
    if (tag.empty() || tag.size() > 100 || tag.contains("..") || tag.contains("//") ||
        tag.front() == '/' || tag.front() == '-' || tag.front() == '.') {
        return false;
    }
    return std::ranges::all_of(tag, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               c == '.' || c == '_' || c == '-' || c == ':' || c == '/';
    });
}

std::string config_name_for(std::string_view tag) {
    if (const auto* known = find_catalog_model(tag)) {
        return std::string(known->name);
    }
    std::string name;
    for (const char c : tag) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            name += c;
        } else if (c >= 'A' && c <= 'Z') {
            name += static_cast<char>(c - 'A' + 'a');
        } else if (!name.empty() && name.back() != '-') {
            name += '-';
        }
    }
    while (!name.empty() && name.back() == '-') {
        name.pop_back();
    }
    if (name.size() > 32) {
        name.resize(32);
    }
    return name.empty() ? std::string("modele") : name;
}

std::string ollama_server(const core::AppConfig& config) {
    for (const auto& model : config.models) {
        const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&model.config);
        if (openai == nullptr || !openai->base_url.contains(kOllamaPort)) {
            continue;
        }
        std::string root = openai->base_url;
        while (root.ends_with('/')) {
            root.pop_back();
        }
        if (root.ends_with("/v1")) {
            root.resize(root.size() - 3);
        }
        return root;
    }
    return "http://localhost:11434";
}

core::Result<std::vector<InstalledModel>> installed_models(const std::string& server) {
    auto listed = detail::get_json(server + "/api/tags", std::chrono::milliseconds{3000});
    if (!listed) {
        return std::unexpected(std::move(listed.error()));
    }
    std::vector<InstalledModel> out;
    if (!listed->is_object() || !listed->contains("models") || !(*listed)["models"].is_array()) {
        return out;
    }
    for (const auto& model : (*listed)["models"]) {
        if (!model.is_object() || !model.contains("name") || !model["name"].is_string()) {
            continue;
        }
        InstalledModel installed{.tag = model["name"].get<std::string>(), .size_bytes = 0};
        if (model.contains("size") && model["size"].is_number_unsigned()) {
            installed.size_bytes = model["size"].get<std::uint64_t>();
        }
        out.push_back(std::move(installed));
    }
    return out;
}

core::Result<void> pull_model(const std::string& server, std::string_view tag,
                              const PullProgress& progress, const std::stop_token& stop) {
    if (!is_valid_model_tag(tag)) {
        return core::make_error(core::ErrorCode::Config, "étiquette de modèle invalide");
    }
    const nlohmann::json body{{"model", std::string(tag)}, {"stream", true}};
    return detail::post_stream(
        detail::HttpRequest{.url = server + "/api/pull",
                            .headers = {},
                            .body = body.dump(),
                            .timeout = std::chrono::seconds{3 * 3600},
                            .ndjson = true},
        [&progress](std::string_view line) -> core::Result<void> {
            const nlohmann::json event =
                nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
            if (!event.is_object()) {
                return {};
            }
            if (event.contains("error") && event["error"].is_string()) {
                return core::make_error(core::ErrorCode::Llm, event["error"].get<std::string>());
            }
            if (progress) {
                const std::string status = event.contains("status") && event["status"].is_string()
                                               ? event["status"].get<std::string>()
                                               : std::string{};
                const auto number = [&event](const char* key) -> std::uint64_t {
                    return event.contains(key) && event[key].is_number_unsigned()
                               ? event[key].get<std::uint64_t>()
                               : 0;
                };
                progress(status, number("completed"), number("total"));
            }
            return {};
        },
        stop);
}

core::Result<void> delete_model(const std::string& server, std::string_view tag) {
    if (!is_valid_model_tag(tag)) {
        return core::make_error(core::ErrorCode::Config, "étiquette de modèle invalide");
    }
    const nlohmann::json body{{"model", std::string(tag)}};
    return detail::delete_json(detail::HttpRequest{.url = server + "/api/delete",
                                                   .headers = {},
                                                   .body = body.dump(),
                                                   .timeout = std::chrono::seconds{60}});
}

} // namespace avenoir::llm
