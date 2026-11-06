// Interface en ligne de commande d'Avenoir.
//
// Usage : avenoir [chemin/vers/config.toml]      (défaut : config.toml)
// Logs  : variable AVENOIR_LOG (trace, debug, info, warn, error ; défaut warn).
//
// Câblage minimal (étape 1.4) : dialogue avec le LLM configuré, sans outils.
// Confirmations, Ctrl+C et /outils arrivent avec l'étape 1.7.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/agent.hpp"
#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/core/version.hpp"
#include "avenoir/llm/factory.hpp"

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

int run(int argc, char** argv) {
    setup_logging();

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

    // Pas encore d'outils (étape 1.5) ni de confirmation interactive (1.7) :
    // toute action à confirmer est refusée.
    core::Agent agent(std::move(*llm), core::ToolRegistry{}, std::make_unique<core::AutoReject>(),
                      std::move(options));

    std::println("avenoir {} — LLM : {} — {} outil(s). Commandes : /reset, /quitter",
                 core::version(), agent.llm_name(), agent.tools().size());

    std::string line;
    while (true) {
        std::print("\n> ");
        (void)std::fflush(stdout);
        if (!std::getline(std::cin, line)) {
            std::println("");
            break; // Fin de l'entrée (Ctrl+D).
        }
        if (line.empty()) {
            continue;
        }
        if (line == "/quitter" || line == "/quit" || line == "/q") {
            break;
        }
        if (line == "/reset") {
            agent.reset();
            std::println("Conversation réinitialisée.");
            continue;
        }
        auto reply = agent.handle(line);
        if (reply) {
            std::println("{}", *reply);
        } else {
            report_error(core::describe(reply.error()).c_str());
        }
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
