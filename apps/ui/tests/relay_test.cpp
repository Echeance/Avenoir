#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "relay.hpp"

using namespace avenoir;
using nlohmann::json;

namespace {

/// Messages postés à la page, attendus d'un autre fil.
struct Page {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<json> messages;

    void post(const json& message) {
        const std::scoped_lock lock(mutex);
        messages.push_back(message);
        changed.notify_all();
    }

    json wait_for(std::string_view type) {
        std::unique_lock lock(mutex);
        json found;
        changed.wait_for(lock, std::chrono::seconds{5}, [&] {
            for (const auto& message : messages) {
                if (message.value("type", "") == type) {
                    found = message;
                    return true;
                }
            }
            return false;
        });
        return found;
    }
};

} // namespace

TEST_CASE("relais : la page ne peut demander que les appels permis", "[ui]") {
    Page page;
    std::vector<std::string> called;
    std::mutex called_mutex;
    {
        ui::Relay relay(ui::DaemonLink{.call = [&](std::string_view method, const json& /*params*/,
                                                   const std::stop_token&) -> core::Result<json> {
                            const std::scoped_lock lock(called_mutex);
                            called.emplace_back(method);
                            return json{{"quit", false}};
                        }},
                        [&page](const json& message) { page.post(message); });
        relay.from_page(R"({"type":"call","id":1,"method":"input","params":{"line":"bonjour"}})");
        CHECK(page.wait_for("reply").value("id", 0) == 1);

        relay.from_page(R"({"type":"call","id":2,"method":"settings/change","params":{}})");
        CHECK(page.wait_for("failure").value("message", "") == "appel refusé");
        relay.from_page(R"({"type":"call","id":3,"method":"input","params":"pas un objet"})");
        relay.from_page(R"({"type":"call","id":-4,"method":"input","params":{}})");
        relay.from_page("pas du JSON");
        relay.from_page(std::string(ui::kMaxPageMessage + 1, ' '));
    }
    CHECK(called == std::vector<std::string>{"input"});
}

TEST_CASE("relais : questions du démon et réponses de la page", "[ui]") {
    Page page;
    ui::Relay relay(ui::DaemonLink{}, [&page](const json& message) { page.post(message); });

    json answer;
    std::jthread asking([&] { answer = relay.ask("confirm", json{{"spec", {}}}, {}); });
    const json question = page.wait_for("question");
    REQUIRE(question.value("method", "") == "confirm");
    // Réponse à une autre question : ignorée ; réponse inattendue : refus.
    relay.from_page(json{{"type", "answer"}, {"id", 999}, {"result", true}}.dump());
    relay.from_page(json{{"type", "answer"}, {"id", question["id"]}, {"result", "oui !"}}.dump());
    asking.join();
    CHECK(answer == false);

    {
        const std::scoped_lock lock(page.mutex);
        page.messages.clear();
    }
    std::jthread always([&] { answer = relay.ask("confirm", json::object(), {}); });
    const json second = page.wait_for("question");
    relay.from_page(json{{"type", "answer"}, {"id", second["id"]}, {"result", "always"}}.dump());
    always.join();
    CHECK(answer == "always");
}

TEST_CASE("relais : réponses validées par type de question", "[ui]") {
    CHECK(ui::checked_answer("confirm", "always") == "always");
    CHECK(ui::checked_answer("confirm", true) == true);
    CHECK(ui::checked_answer("confirm", "toujours") == false);
    CHECK(ui::checked_answer("ask_yes_no", "oui") == false);
    CHECK(ui::checked_answer("ask", json{{"answer", "Lyon"}}) == json{{"answer", "Lyon"}});
    CHECK(ui::checked_answer("ask", json{{"answer", 3}}) == json{{"answer", nullptr}});
    CHECK(ui::checked_answer("ask_secret", "sk-1") == "sk-1");
    CHECK(ui::checked_answer("ask_secret", json::array()).is_null());
    CHECK_FALSE(ui::is_question("install_package"));
}

TEST_CASE("relais : une question annulée ou une page fermée vaut un refus", "[ui]") {
    Page page;
    std::stop_source stop;
    json answer = true;
    {
        ui::Relay relay(ui::DaemonLink{}, [&page](const json& message) { page.post(message); });
        std::jthread asking(
            [&] { answer = relay.ask("ask_yes_no", json::object(), stop.get_token()); });
        page.wait_for("question");
        stop.request_stop();
        asking.join();
        CHECK(answer == false);
        CHECK(page.wait_for("dismiss").contains("id"));

        // Fermeture du relais pendant une question : refus.
        json secret = "x";
        std::jthread pending([&] { secret = relay.ask("ask_secret", json::object(), {}); });
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        relay.disconnected();
        pending.join();
        CHECK(secret.is_null());
    }
}

TEST_CASE("relais : l'application s'ouvre une fois, même sur un double clic", "[ui]") {
    int opened = 0;
    ui::Relay relay(ui::DaemonLink{}, [](const json& /*message*/) {}, {}, [&opened] { ++opened; });
    relay.from_page(R"({"type":"open_app"})");
    relay.from_page(R"({"type":"open_app"})");
    CHECK(opened == 1);
    // Sans fonction d'ouverture (application elle-même) : rien.
    ui::Relay inside(ui::DaemonLink{}, [](const json& /*message*/) {});
    inside.from_page(R"({"type":"open_app"})");
    CHECK(ui::is_allowed_call("journal/list"));
    CHECK(ui::is_allowed_call("models/list"));
    CHECK_FALSE(ui::is_allowed_call("models/delete"));
}

TEST_CASE("relais : dossiers des onglets web gardés par l'hôte", "[ui]") {
    Page page;
    ui::Relay relay(ui::DaemonLink{.call = [](std::string_view, const json&,
                                              const std::stop_token&) -> core::Result<json> {
                        return json{
                            {"tabs",
                             {{{"module", "meteo"},
                               {"id", "carte"},
                               {"kind", "web"},
                               {"directory", "/modules/meteo/ui"}},
                              {{"module", "notes"}, {"id", "notes"}, {"kind", "declarative"}}}}};
                    }},
                    [&page](const json& message) { page.post(message); });
    CHECK_FALSE(relay.module_root("meteo").has_value());
    relay.from_page(R"({"type":"call","id":1,"method":"tabs/list","params":{}})");
    const json reply = page.wait_for("reply");
    // La page ne voit pas le chemin ; l'hôte s'en souvient.
    REQUIRE(reply["result"]["tabs"].size() == 2);
    CHECK_FALSE(reply["result"]["tabs"][0].contains("directory"));
    CHECK(relay.module_root("meteo") == std::filesystem::path("/modules/meteo/ui"));
    CHECK_FALSE(relay.module_root("notes").has_value());
}
