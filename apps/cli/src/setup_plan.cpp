#include "setup_plan.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/llm/discovery.hpp"

namespace avenoir::cli {

namespace {

/// Modèles évalués par le banc d'essai (evals/resultats/COMPARAISON.md,
/// série 9), dans l'ordre où ils sont conseillés.
struct BenchedModel {
    std::string_view id;
    std::string_view note;
};
constexpr std::array<BenchedModel, 3> kBenched{{
    {.id = "gemma4:26b-a4b",
     .note = "conseillé : le plus sûr au quotidien et rapide (banc : 125/126)"},
    {.id = "qwen3.8:27b-q4_K_M",
     .note = "conseillé pour le code et la réflexion, plus lent (banc : 126/126)"},
    {.id = "ministral-3:8b", .note = "léger, pour une petite carte graphique (banc : 116/126)"},
}};

constexpr std::string_view kOllamaPort = ":11434/";

std::optional<std::size_t> bench_rank(std::string_view id) {
    for (std::size_t i = 0; i < kBenched.size(); ++i) {
        if (kBenched.at(i).id == id) {
            return i;
        }
    }
    return std::nullopt;
}

/// « Ollama sur cette machine », « LM Studio sur 192.168.1.20 »…
std::string server_label(const std::string& base_url, bool this_machine) {
    std::string_view kind = "serveur";
    if (base_url.contains(kOllamaPort)) {
        kind = "Ollama";
    } else if (base_url.contains(":1234/")) {
        kind = "LM Studio";
    } else if (base_url.contains(":8080/")) {
        kind = "llama.cpp";
    }
    if (this_machine) {
        return std::format("{} sur cette machine", kind);
    }
    std::string_view host = base_url;
    if (const auto scheme = host.find("://"); scheme != std::string_view::npos) {
        host.remove_prefix(scheme + 3);
    }
    host = host.substr(0, host.find('/'));
    return std::format("{} sur {}", kind, host);
}

void add_servers(std::vector<SetupCandidate>& out, std::vector<std::string>& taken,
                 const std::vector<llm::DiscoveredServer>& servers, bool this_machine,
                 bool remote) {
    for (const auto& server : servers) {
        for (const auto& id : server.models) {
            core::OpenAiCompatibleConfig config{.base_url = server.base_url,
                                                .model = id,
                                                .api_key_env = std::nullopt,
                                                .temperature = 0.2,
                                                .reasoning_effort = std::nullopt};
            if (server.base_url.contains(kOllamaPort)) {
                // Ollama : les modèles qui réfléchissent répondent bien plus vite.
                config.reasoning_effort = "none";
            }
            const auto rank = bench_rank(id);
            SetupCandidate candidate{
                .model = core::NamedLlm{.name = model_name_for(id, taken),
                                        .config = std::move(config),
                                        .local = std::nullopt},
                .label = std::format("{}, {}", id, server_label(server.base_url, this_machine)),
                .note = rank ? std::string(kBenched.at(*rank).note)
                             : std::string("non évalué par le banc d'essai"),
                .remote = remote,
                .recommended = rank.has_value(),
            };
            if (!this_machine && !remote) {
                candidate.model.local = true; // serveur du réseau, à l'utilisateur
            }
            if (remote) {
                candidate.note += ", distant : la conversation quitte votre réseau";
            }
            taken.push_back(candidate.model.name);
            out.push_back(std::move(candidate));
        }
    }
}

} // namespace

std::string model_name_for(std::string_view model_id, const std::vector<std::string>& taken) {
    std::string_view base = model_id;
    if (const auto slash = base.rfind('/'); slash != std::string_view::npos) {
        base.remove_prefix(slash + 1); // « éditeur/modèle » (LM Studio)
    }
    base = base.substr(0, base.find(':')); // sans l'étiquette d'Ollama
    std::string name;
    for (const char c : base) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 && byte < 0x80U) {
            name += static_cast<char>(std::tolower(byte));
        } else if (c == '_' || c == '-') {
            name += c;
        } else if (!name.empty() && name.back() != '-') {
            name += '-';
        }
    }
    while (!name.empty() && name.back() == '-') {
        name.pop_back();
    }
    constexpr std::size_t kMaxName = 28; // place pour « -99 » sous la limite de 32
    if (name.size() > kMaxName) {
        name.resize(kMaxName);
    }
    if (name.empty()) {
        name = "modele";
    }
    if (!std::ranges::contains(taken, name)) {
        return name;
    }
    for (int suffix = 2;; ++suffix) {
        std::string candidate = std::format("{}-{}", name, suffix);
        if (!std::ranges::contains(taken, candidate)) {
            return candidate;
        }
    }
}

std::vector<SetupCandidate> make_candidates(const SetupFindings& findings, bool local_only) {
    std::vector<SetupCandidate> local;
    std::vector<std::string> taken;
    add_servers(local, taken, findings.local_servers, /*this_machine=*/true, /*remote=*/false);
    add_servers(local, taken, findings.owned_servers, /*this_machine=*/false, /*remote=*/false);
    // Conseillés d'abord, dans l'ordre du banc ; les autres gardent le leur.
    std::ranges::stable_sort(local, [](const SetupCandidate& a, const SetupCandidate& b) {
        const auto* first = std::get_if<core::OpenAiCompatibleConfig>(&a.model.config);
        const auto* second = std::get_if<core::OpenAiCompatibleConfig>(&b.model.config);
        const auto rank_a = bench_rank(first->model).value_or(kBenched.size());
        const auto rank_b = bench_rank(second->model).value_or(kBenched.size());
        return rank_a < rank_b;
    });
    if (local_only) {
        return local;
    }
    add_servers(local, taken, findings.remote_servers, /*this_machine=*/false, /*remote=*/true);
    if (findings.claude_code) {
        local.push_back(SetupCandidate{
            .model = core::NamedLlm{.name = model_name_for("claude", taken),
                                    .config = core::ClaudeCodeConfig{},
                                    .local = std::nullopt},
            .label = "Claude par Claude Code (votre abonnement Claude)",
            .note = "distant : la conversation part chez Anthropic (banc, haiku : 82/84)",
            .remote = true,
            .recommended = false,
        });
        taken.push_back(local.back().model.name);
    }
    if (findings.anthropic_key) {
        local.push_back(SetupCandidate{
            .model =
                core::NamedLlm{.name = model_name_for("sonnet", taken),
                               .config = core::AnthropicConfig{.model = "claude-sonnet-5-5",
                                                               .api_key_env = "ANTHROPIC_API_KEY",
                                                               .max_tokens = 4096,
                                                               .base_url = std::nullopt},
                               .local = std::nullopt},
            .label = "Claude Sonnet 5.5 par l'API Anthropic (clé ANTHROPIC_API_KEY)",
            .note = "distant et payant à l'usage : la conversation part chez Anthropic",
            .remote = true,
            .recommended = false,
        });
    }
    return local;
}

std::optional<std::vector<std::size_t>> parse_selection(std::string_view text, std::size_t count) {
    std::vector<std::size_t> chosen;
    std::size_t index = 0;
    while (index < text.size()) {
        const char c = text[index];
        if (c == ' ' || c == ',' || c == '\t' || c == '\r') {
            ++index;
            continue;
        }
        std::size_t value = 0;
        const std::string_view rest = text.substr(index);
        const auto [next, error] =
            std::from_chars(std::to_address(rest.begin()), std::to_address(rest.end()), value);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        index += static_cast<std::size_t>(next - rest.data());
        if (value < 1 || value > count) {
            return std::nullopt;
        }
        if (!std::ranges::contains(chosen, value - 1)) {
            chosen.push_back(value - 1);
        }
    }
    if (chosen.empty()) {
        return std::nullopt;
    }
    return chosen;
}

core::AppConfig build_setup_config(const std::vector<SetupCandidate>& chosen, bool local_only,
                                   const core::AppConfig& base) {
    core::AppConfig config = base;
    config.models.clear();
    for (const auto& candidate : chosen) {
        config.models.push_back(candidate.model);
    }
    config.agent.local_only = local_only;
    return config;
}

std::optional<std::string> normalize_server_url(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '/')) {
        text.remove_suffix(1);
    }
    const bool http = text.starts_with("http://") || text.starts_with("https://");
    const bool clean = std::ranges::none_of(text, [](char c) {
        return static_cast<unsigned char>(c) <= 0x20U || c == 0x7F || c == '@';
    });
    if (!http || !clean || text.size() < 10 || text.size() > 300) {
        return std::nullopt;
    }
    std::string url(text);
    if (!url.ends_with("/v1")) {
        url += "/v1";
    }
    return url;
}

} // namespace avenoir::cli
