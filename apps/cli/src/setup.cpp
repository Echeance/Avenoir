#include "setup.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"
#include "avenoir/llm/discovery.hpp"

#include "console.hpp"
#include "conversations.hpp"
#include "display.hpp"
#include "setup_plan.hpp"

namespace avenoir::cli {

namespace {

/// Délai pour un serveur dont l'adresse a été saisie : il peut être loin.
constexpr std::chrono::seconds kTypedServerTimeout{5};
constexpr std::size_t kShownModels = 4;

/// Saisie au premier lancement, avant la barre de saisie : ligne de l'entrée
/// standard ; rien en fin de saisie (Ctrl+D, entrée fermée).
std::optional<std::string> ask_terminal(const std::string& question) {
    display().question(question);
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        display().plain("");
        return std::nullopt;
    }
    return answer;
}

/// Une ligne saisie, sans espaces en fin ; rien si l'utilisateur abandonne.
std::optional<std::string> ask(const SetupAsk& io, std::string_view question) {
    auto answer = io(std::string(question));
    while (answer && !answer->empty() && (answer->back() == '\r' || answer->back() == ' ')) {
        answer->pop_back();
    }
    return answer;
}

/// Oui / non, « non » par défaut ; rien en fin de saisie.
std::optional<bool> ask_yes_no(const SetupAsk& io, std::string_view question) {
    while (true) {
        const auto answer = ask(io, std::format("{} [o/N]", question));
        if (!answer) {
            return std::nullopt;
        }
        if (answer->empty() || *answer == "n" || *answer == "N" || *answer == "non") {
            return false;
        }
        if (*answer == "o" || *answer == "O" || *answer == "oui" || *answer == "y") {
            return true;
        }
        display().warning("Répondez « o » (oui) ou « n » (non).");
    }
}

/// Exécutable `name` présent dans le PATH ?
bool in_path(std::string_view name) {
    const auto path = core::read_env("PATH");
    if (!path) {
        return false;
    }
#ifdef _WIN32
    constexpr char kSeparator = ';';
    const std::vector<std::string> suffixes{".exe", ".cmd", ".bat"};
#else
    constexpr char kSeparator = ':';
    const std::vector<std::string> suffixes{""};
#endif
    std::string_view rest = *path;
    while (!rest.empty()) {
        const auto end = rest.find(kSeparator);
        const std::string_view entry = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (entry.empty()) {
            continue; // dossier courant : jamais
        }
        for (const auto& suffix : suffixes) {
            std::error_code error;
            const auto candidate = std::filesystem::path(entry) / (std::string(name) + suffix);
            if (std::filesystem::is_regular_file(candidate, error)) {
                return true;
            }
        }
    }
    return false;
}

std::string models_summary(const std::vector<std::string>& models) {
    std::string text;
    for (std::size_t i = 0; i < models.size() && i < kShownModels; ++i) {
        text += (i == 0 ? "" : ", ") + models[i];
    }
    if (models.size() > kShownModels) {
        text += std::format("… ({} en tout)", models.size());
    }
    return text;
}

/// Sondage du réseau local, interrompu par Ctrl+C.
std::vector<llm::DiscoveredServer> scan_network(const std::vector<std::string>& hosts) {
    std::vector<llm::DiscoveredServer> servers;
    std::atomic<bool> done{false};
    std::jthread scan([&servers, &hosts, &done](const std::stop_token& stop) {
        servers = llm::discover_servers(hosts, stop);
        done = true;
    });
    (void)take_interrupt();
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        if (take_interrupt()) {
            scan.request_stop();
            display().info("Recherche sur le réseau arrêtée.");
            break;
        }
    }
    scan.join(); // `servers` n'est lu qu'une fois le fil terminé
    return servers;
}

/// Serveurs du réseau local, chacun retenu seulement si l'utilisateur le
/// déclare à lui. Rien en fin de saisie.
std::optional<std::vector<llm::DiscoveredServer>> find_network_servers(const SetupAsk& io) {
    std::vector<llm::DiscoveredServer> owned;
    const auto address = llm::local_network_address();
    if (!address) {
        return owned; // pas de réseau privé : jamais de sondage
    }
    const std::string network = address->substr(0, address->rfind('.'));
    const auto wanted = ask_yes_no(
        io,
        std::format(
            "Chercher aussi des serveurs de modèles sur votre réseau local ({}.0/24, ports {}, {} "
            "et {}) ? À ne faire que sur votre propre réseau.",
            network, llm::kDefaultServerPorts[0], llm::kDefaultServerPorts[1],
            llm::kDefaultServerPorts[2]));
    if (!wanted) {
        return std::nullopt;
    }
    if (!*wanted) {
        return owned;
    }
    display().info("Recherche sur le réseau… (quelques secondes, Ctrl+C pour arrêter)");
    const auto found = scan_network(llm::subnet_neighbours(*address));
    if (found.empty()) {
        display().info("Aucun serveur de modèles trouvé sur le réseau.");
    }
    for (const auto& server : found) {
        if (server.models.empty()) {
            continue;
        }
        const auto mine = ask_yes_no(
            io, std::format("Trouvé : {} ({}). Cette machine est-elle à vous ? Vos conversations y "
                            "seront envoyées.",
                            server.base_url, models_summary(server.models)));
        if (!mine) {
            return std::nullopt;
        }
        if (*mine) {
            owned.push_back(server);
        }
    }
    return owned;
}

/// Serveur dont l'adresse est saisie à la main (facultatif). Faux en fin de
/// saisie.
bool find_typed_server(const SetupAsk& io, SetupFindings& findings) {
    while (true) {
        const auto typed = ask(
            io, "Adresse d'un autre serveur compatible OpenAI (http://hôte:port), ou Entrée pour "
                "passer :");
        if (!typed) {
            return false;
        }
        if (typed->empty()) {
            return true;
        }
        const auto url = normalize_server_url(*typed);
        if (!url) {
            display().warning("Adresse invalide : http(s)://hôte:port attendu, sans identifiants "
                              "(une clé d'API se règle ensuite dans la configuration).");
            continue;
        }
        auto models = llm::list_server_models(*url, kTypedServerTimeout);
        if (!models) {
            display().warning(core::describe(models.error()));
            continue;
        }
        if (models->empty()) {
            display().warning(std::format("{} ne sert aucun modèle.", *url));
            continue;
        }
        const auto mine = ask_yes_no(
            io,
            std::format(
                "{} ({}) : ce serveur est-il à vous, sur votre réseau ? (Non : il sera tenu pour "
                "distant et désactivé en mode « local uniquement ».)",
                *url, models_summary(*models)));
        if (!mine) {
            return false;
        }
        llm::DiscoveredServer server{.base_url = *url, .models = std::move(*models)};
        (*mine ? findings.owned_servers : findings.remote_servers).push_back(std::move(server));
        return true;
    }
}

/// Numéros choisis parmi `count` ; Entrée : `fallback` ; « aucun » : liste
/// vide si `allow_none`. Rien en fin de saisie.
std::optional<std::vector<std::size_t>> ask_selection(const SetupAsk& io, std::string_view question,
                                                      std::size_t count, std::size_t fallback,
                                                      bool allow_none = false) {
    while (true) {
        const auto answer = ask(io, question);
        if (!answer) {
            return std::nullopt;
        }
        if (answer->empty()) {
            return std::vector<std::size_t>{fallback};
        }
        if (allow_none && (*answer == "aucun" || *answer == "0")) {
            return std::vector<std::size_t>{};
        }
        if (auto chosen = parse_selection(*answer, count)) {
            return chosen;
        }
        display().warning(
            std::format("Numéros de 1 à {} attendus, séparés par des espaces.", count));
    }
}

void show_candidates(const std::vector<SetupCandidate>& candidates) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        display().plain(std::format("  {:>2}. {}", i + 1, candidate.label));
        display().info(std::format("      {}", candidate.note));
    }
}

/// Enregistre `config` à `path` et le dit.
SetupResult save(const std::filesystem::path& path, core::AppConfig config) {
    if (auto saved = core::save_config(path, config); !saved) {
        display().error(core::describe(saved.error()));
        display().warning("Configuration utilisée pour cette session seulement.");
        return SetupResult{.config = std::move(config), .saved = false};
    }
    display().success(std::format("Configuration enregistrée : {}", utf8_path(path)));
    return SetupResult{.config = std::move(config), .saved = true};
}

/// Aucun modèle : continuer avec la voie rapide seule (enregistré, la
/// création ne revient pas d'elle-même) ou non (rien n'est écrit).
std::optional<SetupResult> without_models(const std::filesystem::path& path, core::AppConfig config,
                                          const SetupAsk& io) {
    const auto keep = ask_yes_no(
        io, "Continuer sans modèle, avec la voie rapide seule ? (Non : rien n'est enregistré, "
            "la création sera proposée de nouveau au prochain lancement.)");
    if (!keep) {
        return std::nullopt;
    }
    if (!*keep) {
        return SetupResult{.config = std::move(config), .saved = false};
    }
    auto result = save(path, std::move(config));
    display().info("Sans modèle, seules les commandes simples répondent (voie rapide). Pour en "
                   "ajouter un plus tard : /configurer.");
    return result;
}

} // namespace

std::optional<SetupResult> run_setup(const std::filesystem::path& path, const core::AppConfig& base,
                                     const SetupAsk& io) {
    display().info("Recherche des modèles sur cette machine…");
    SetupFindings findings;
    findings.local_servers = llm::discover_local_servers();
    for (const auto& server : findings.local_servers) {
        display().info(
            std::format("Trouvé : {} ({}).", server.base_url, models_summary(server.models)));
    }
    auto network = find_network_servers(io);
    if (!network) {
        return std::nullopt;
    }
    findings.owned_servers = std::move(*network);
    if (!find_typed_server(io, findings)) {
        return std::nullopt;
    }
    findings.claude_code = in_path("claude");
    findings.anthropic_key = core::read_env("ANTHROPIC_API_KEY").has_value();

    const auto all = make_candidates(findings, /*local_only=*/false);
    const bool has_local = std::ranges::any_of(all, [](const auto& c) { return !c.remote; });
    const bool has_remote = std::ranges::any_of(all, [](const auto& c) { return c.remote; });
    bool local_only = base.agent.local_only;
    if (has_local && has_remote) {
        const auto answer = ask_yes_no(
            io, "Mode « local uniquement » ? Vos conversations ne quitteront jamais votre "
                "machine ni votre réseau : les modèles distants seront écartés.");
        if (!answer) {
            return std::nullopt;
        }
        local_only = *answer;
    }
    const auto candidates = local_only ? make_candidates(findings, true) : all;

    if (candidates.empty()) {
        display().warning("Aucun modèle trouvé.");
        display().info("Pour un modèle local : installez Ollama (https://ollama.com), puis "
                       "« ollama pull gemma4:26b-a4b ».");
        return without_models(path, build_setup_config({}, local_only, base), io);
    }

    display().plain("");
    display().plain("Modèles disponibles :");
    show_candidates(candidates);
    const auto chosen_indices =
        ask_selection(io,
                      std::format("Modèles à utiliser (numéros séparés par des espaces ; "
                                  "« aucun » : voie rapide seule ; Entrée : 1, {}) :",
                                  candidates.front().model.name),
                      candidates.size(), 0, /*allow_none=*/true);
    if (!chosen_indices) {
        return std::nullopt;
    }
    if (chosen_indices->empty()) {
        return without_models(path, build_setup_config({}, local_only, base), io);
    }
    std::vector<SetupCandidate> chosen;
    for (const auto index : *chosen_indices) {
        chosen.push_back(candidates[index]);
    }
    if (chosen.size() > 1) {
        display().plain("Modèle par défaut, au lancement :");
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            display().plain(std::format("  {:>2}. {}", i + 1, chosen[i].model.name));
        }
        const auto first =
            ask_selection(io, std::format("Numéro (Entrée : 1, {}) :", chosen.front().model.name),
                          chosen.size(), 0);
        if (!first) {
            return std::nullopt;
        }
        // Le modèle choisi passe en tête, les autres gardent leur ordre.
        const auto picked = chosen.begin() + static_cast<std::ptrdiff_t>(first->front());
        std::rotate(chosen.begin(), picked, picked + 1);
    }

    auto result = save(path, build_setup_config(chosen, local_only, base));
    if (chosen.size() > 1) {
        display().info(std::format("Changer de modèle à tout moment : « passe sur {} ».",
                                   chosen[1].model.name));
    }
    return result;
}

std::optional<core::AppConfig> run_first_setup(const std::filesystem::path& path) {
    display().info(
        std::format("Première utilisation : préparons la configuration ({}).", utf8_path(path)));
    auto result = run_setup(path, core::AppConfig{}, ask_terminal);
    if (!result) {
        return std::nullopt;
    }
    return std::move(result->config);
}

} // namespace avenoir::cli
