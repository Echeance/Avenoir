#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/ipc/peer.hpp"
#include "avenoir/ipc/transport.hpp"

using namespace avenoir;
using nlohmann::json;

namespace {

/// Adresse propre au test (socket dans un dossier temporaire court, ou tube).
ipc::Endpoint test_endpoint(const char* name) {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
    return ipc::Endpoint{.address = "\\\\.\\pipe\\avenoir-test-" + std::string(name) + unique};
#else
    const std::filesystem::path directory =
        std::filesystem::path("/tmp") / ("avenoir-ipc-" + unique.substr(unique.size() - 8));
    return ipc::Endpoint{.address = (directory / (std::string(name) + ".sock")).string()};
#endif
}

void cleanup(const ipc::Endpoint& endpoint) {
#ifndef _WIN32
    std::error_code ignored;
    std::filesystem::remove_all(std::filesystem::path(endpoint.address).parent_path(), ignored);
#else
    (void)endpoint;
#endif
}

/// Un serveur et un client reliés, chacun avec son `Peer`.
struct Pair {
    std::unique_ptr<ipc::Peer> server;
    std::unique_ptr<ipc::Peer> client;
};

Pair connect_pair(ipc::Listener& listener, const ipc::Endpoint& endpoint) {
    std::unique_ptr<ipc::Connection> accepted;
    std::jthread acceptor([&](const std::stop_token& stop) {
        auto connection = listener.accept(stop);
        if (connection) {
            accepted = std::move(*connection);
        }
    });
    auto client = ipc::connect(endpoint);
    REQUIRE(client.has_value());
    acceptor.join();
    REQUIRE(accepted != nullptr);
    return {.server = std::make_unique<ipc::Peer>(std::move(accepted)),
            .client = std::make_unique<ipc::Peer>(std::move(*client))};
}

} // namespace

TEST_CASE("ipc : lignes dans les deux sens", "[ipc]") {
    const auto endpoint = test_endpoint("lignes");
    auto listener = ipc::listen(endpoint);
    REQUIRE(listener.has_value());
    std::unique_ptr<ipc::Connection> server;
    std::jthread acceptor([&](const std::stop_token& stop) {
        if (auto accepted = (*listener)->accept(stop)) {
            server = std::move(*accepted);
        }
    });
    auto client = ipc::connect(endpoint);
    REQUIRE(client.has_value());
    acceptor.join();
    REQUIRE(server != nullptr);

    REQUIRE((*client)->write_line("bonjour").has_value());
    REQUIRE((*client)->write_line("é 🐧").has_value());
    const std::stop_source never;
    CHECK(server->read_line(never.get_token()).value() == "bonjour");
    CHECK(server->read_line(never.get_token()).value() == "é 🐧");
    REQUIRE(server->write_line("réponse").has_value());
    CHECK((*client)->read_line(never.get_token()).value() == "réponse");

    // Saut de ligne dans un message : refusé (il couperait le message en deux).
    CHECK_FALSE((*client)->write_line("deux\nlignes").has_value());

    // Fermeture : l'autre côté lit la fin.
    (*client)->close();
    const auto end = server->read_line(never.get_token());
    REQUIRE(end.has_value());
    CHECK_FALSE(end->has_value());
    cleanup(endpoint);
}

TEST_CASE("ipc : une seule instance à une adresse", "[ipc]") {
    const auto endpoint = test_endpoint("unique");
    auto first = ipc::listen(endpoint);
    REQUIRE(first.has_value());
    CHECK_FALSE(ipc::listen(endpoint).has_value());
    first->reset();
#ifndef _WIN32
    // Adresse reprise une fois le premier arrêté.
    CHECK(ipc::listen(endpoint).has_value());
#endif
    cleanup(endpoint);
}

TEST_CASE("ipc : personne n'écoute", "[ipc]") {
    const auto endpoint = test_endpoint("absent");
    const auto connection = ipc::connect(endpoint);
    REQUIRE_FALSE(connection.has_value());
    CHECK(connection.error().code == core::ErrorCode::Ipc);
    cleanup(endpoint);
}

TEST_CASE("ipc : attente interrompue", "[ipc]") {
    const auto endpoint = test_endpoint("arret");
    auto listener = ipc::listen(endpoint);
    REQUIRE(listener.has_value());
    std::stop_source stop; // NOLINT(misc-const-correctness) : request_stop n'est pas const
    const std::jthread stopper([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop.request_stop();
    });
    const auto accepted = (*listener)->accept(stop.get_token());
    REQUIRE_FALSE(accepted.has_value());
    CHECK(accepted.error().code == core::ErrorCode::Cancelled);
    cleanup(endpoint);
}

TEST_CASE("Peer : appels, notifications et erreurs", "[ipc]") {
    const auto endpoint = test_endpoint("peer");
    auto listener = ipc::listen(endpoint);
    REQUIRE(listener.has_value());
    auto pair = connect_pair(**listener, endpoint);

    std::atomic<int> notified{0};
    pair.server->on_request("addition", [](const json& params, const std::stop_token&) {
        return core::Result<json>(params.at("a").get<int>() + params.at("b").get<int>());
    });
    pair.server->on_request("echec", [](const json&, const std::stop_token&) -> core::Result<json> {
        return core::make_error(core::ErrorCode::Module, "raté");
    });
    pair.server->on_notification("bip", [&notified](const json&) { ++notified; });
    pair.server->start();
    pair.client->start();

    const std::stop_source never;
    CHECK(pair.client->call("addition", {{"a", 2}, {"b", 3}}, never.get_token()).value() == 5);

    const auto failed = pair.client->call("echec", json::object(), never.get_token());
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == core::ErrorCode::Module); // catégorie transmise
    CHECK(failed.error().message == "raté");

    const auto unknown = pair.client->call("inconnue", json::object(), never.get_token());
    REQUIRE_FALSE(unknown.has_value());

    REQUIRE(pair.client->notify("bip", json::object()).has_value());
    REQUIRE(pair.client->notify("bip", json::object()).has_value());
    // La notification arrive avant la réponse à l'appel suivant (ordre gardé).
    CHECK(pair.client->call("addition", {{"a", 1}, {"b", 1}}, never.get_token()).value() == 2);
    CHECK(notified == 2);
    cleanup(endpoint);
}

TEST_CASE("Peer : appel en sens inverse pendant une requête", "[ipc]") {
    // Le client demande une requête ; pendant qu'elle tourne, le serveur
    // demande une confirmation au client : pas d'interblocage.
    const auto endpoint = test_endpoint("inverse");
    auto listener = ipc::listen(endpoint);
    REQUIRE(listener.has_value());
    auto pair = connect_pair(**listener, endpoint);
    ipc::Peer* server = pair.server.get();

    pair.server->on_request("requete", [server](const json&, const std::stop_token& stop) {
        auto confirmed = server->call("confirmer", {{"action", "éteindre"}}, stop);
        if (!confirmed) {
            return core::Result<json>(std::unexpected(confirmed.error()));
        }
        return core::Result<json>(confirmed->get<bool>() ? "fait" : "refusé");
    });
    pair.client->on_request("confirmer", [](const json& params, const std::stop_token&) {
        return core::Result<json>(params.at("action") == "éteindre");
    });
    pair.server->start();
    pair.client->start();

    const std::stop_source never;
    CHECK(pair.client->call("requete", json::object(), never.get_token()).value() == "fait");
    cleanup(endpoint);
}

TEST_CASE("Peer : annulation et fermeture", "[ipc]") {
    const auto endpoint = test_endpoint("annulation");
    auto listener = ipc::listen(endpoint);
    REQUIRE(listener.has_value());
    auto pair = connect_pair(**listener, endpoint);

    std::atomic<bool> handler_stopped{false};
    pair.server->on_request("longue", [&handler_stopped](const json&, const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        handler_stopped = true;
        return core::Result<json>(core::make_error(core::ErrorCode::Cancelled, {}));
    });
    std::atomic<bool> closed{false};
    pair.client->on_closed([&closed] { closed = true; });
    pair.server->start();
    pair.client->start();

    // Ctrl+C côté client : l'appel s'arrête et la requête du serveur aussi.
    std::stop_source stop; // NOLINT(misc-const-correctness) : request_stop n'est pas const
    const std::jthread stopper([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        stop.request_stop();
    });
    const auto cancelled = pair.client->call("longue", json::object(), stop.get_token());
    REQUIRE_FALSE(cancelled.has_value());
    CHECK(cancelled.error().code == core::ErrorCode::Cancelled);
    for (int i = 0; i < 100 && !handler_stopped; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(handler_stopped);

    // Le serveur s'en va : le client le voit, ses appels échouent.
    pair.server.reset();
    for (int i = 0; i < 100 && !closed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(closed);
    const std::stop_source never;
    CHECK_FALSE(pair.client->call("longue", json::object(), never.get_token()).has_value());
    cleanup(endpoint);
}
