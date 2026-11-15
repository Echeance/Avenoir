// Hôte des fenêtres d'Avenoir (exécutable `avenoir-ui`) : le widget en haut
// de l'écran, une page web affichée par la vue web du système. C'est un
// client du démon comme la CLI : il s'y connecte (et le lance s'il ne répond
// pas), relaie ses messages à la page et les réponses de la page au démon.
//
// Usage : avenoir-ui [config.toml]

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/env.hpp"
#include "avenoir/ipc/peer.hpp"

#include "launcher.hpp"
#include "relay.hpp"
#include "window.hpp"

namespace {

using namespace avenoir;
using nlohmann::json;

void setup_logging() {
    const auto logger = spdlog::stderr_color_mt("avenoir-ui");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%^[%n %l]%$ %v");
    spdlog::set_level(spdlog::level::warn);
    if (const auto level = core::read_env("AVENOIR_LOG")) {
        spdlog::set_level(spdlog::level::from_str(*level));
    }
}

int run(int argc, char** argv) {
    setup_logging();
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    std::optional<std::filesystem::path> config;
    if (args.size() > 1) {
        config = std::filesystem::path(args[1]);
    }
    std::filesystem::path ui_directory;
    if (const auto chosen = core::read_env("AVENOIR_UI_DIR")) {
        ui_directory = *chosen; // développement : page construite ailleurs
    } else if (const auto beside = ui::default_ui_directory()) {
        ui_directory = *beside;
    }
    std::error_code missing;
    if (!std::filesystem::is_regular_file(ui_directory / "index.html", missing)) {
        spdlog::error("page de l'interface introuvable : {} (construire ui/ : npm run build)",
                      (ui_directory / "index.html").string());
        return EXIT_FAILURE;
    }

    auto endpoint = cli::daemon_endpoint(config);
    if (!endpoint) {
        spdlog::error("{}", core::describe(endpoint.error()));
        return EXIT_FAILURE;
    }
    auto connection = cli::connect_daemon(*endpoint, config);
    if (!connection) {
        spdlog::error("{}", core::describe(connection.error()));
        return EXIT_FAILURE;
    }

    // Ordre de destruction : le pair (ses fils appellent le relais) avant le
    // relais, le relais (ses fils postent à la fenêtre) avant la fenêtre.
    std::unique_ptr<ui::Window> window;
    std::unique_ptr<ui::Relay> relay;
    auto created = ui::create_window(ui::WindowOptions{
        .ui_directory = ui_directory,
        .from_page =
            [&relay](const std::string& message) {
                if (relay) {
                    relay->from_page(message);
                }
            },
        .developer_tools = core::read_env("AVENOIR_UI_DEBUG").has_value(),
        .pinned = core::read_env("AVENOIR_UI_PIN").has_value(),
    });
    if (!created) {
        spdlog::error("{}", core::describe(created.error()));
        return EXIT_FAILURE;
    }
    window = std::move(*created);

    auto peer = std::make_unique<ipc::Peer>(std::move(*connection));
    relay = std::make_unique<ui::Relay>(
        ui::DaemonLink{
            .call =
                [&peer](std::string_view method, json params, const std::stop_token& stop) {
                    // Comme la CLI : annulé chez le démon, dont la réponse est attendue
                    // (il peut encore proposer d'annuler les actions faites).
                    return peer->call(method, std::move(params), stop, ipc::Peer::OnStop::AwaitEnd);
                }},
        [&window](const json& message) { window->post(message.dump()); },
        [&window](int height) { window->resize(height); });

    peer->on_notification("ui", [&relay](const json& params) { relay->ui(params); });
    for (const char* question : {"ask", "ask_yes_no", "confirm", "ask_secret"}) {
        peer->on_request(question,
                         [&relay, question](const json& params, const std::stop_token& stop) {
                             return core::Result<json>(relay->ask(question, params, stop));
                         });
    }
    // Pas de terminal pour installer un paquet (swaylock) : le démon le dit.
    peer->on_request("install_package", [](const json& /*params*/, const std::stop_token&) {
        return core::Result<json>(
            json{{"ok", false}, {"message", "installation possible seulement depuis la CLI"}});
    });
    peer->on_closed([&relay] { relay->disconnected(); });
    peer->start();

    const int status = window->run();
    peer.reset();
    relay.reset();
    window.reset();
    return status;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        (void)std::fputs("Erreur : ", stderr);
        (void)std::fputs(error.what(), stderr);
        (void)std::fputs("\n", stderr);
    } catch (...) {
        (void)std::fputs("Erreur inattendue\n", stderr);
    }
    return EXIT_FAILURE;
}
