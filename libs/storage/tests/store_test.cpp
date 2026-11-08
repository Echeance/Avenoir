#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/storage/store.hpp"

using namespace avenoir;

namespace {

storage::Store memory_store() {
    auto store = storage::Store::open_in_memory();
    REQUIRE(store.has_value());
    return std::move(*store);
}

using Found = core::Result<std::optional<storage::ConversationInfo>>;

std::string title_of(const Found& found) {
    return found && *found ? (*found)->title : std::string{};
}

std::int64_t id_of(const Found& found) {
    return found && *found ? (*found)->id : -1;
}

std::size_t count_of(const Found& found) {
    return found && *found ? (*found)->message_count : 0;
}

} // namespace

TEST_CASE("créer, lister, renommer, supprimer", "[store]") {
    auto store = memory_store();
    const auto first = store.create_conversation("Première");
    const auto second = store.create_conversation("Seconde");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    auto list = store.list_conversations();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    CHECK(list->front().title == "Seconde"); // la plus récente d'abord

    REQUIRE(store.rename(first->id, "Renommée").has_value());
    CHECK(title_of(store.conversation(first->id)) == "Renommée");

    REQUIRE(store.remove(second->id).has_value());
    list = store.list_conversations();
    REQUIRE(list.has_value());
    CHECK(list->size() == 1);
}

TEST_CASE("messages : ajout, relecture dans l'ordre, retrait du dernier", "[store]") {
    auto store = memory_store();
    const auto conversation = store.create_conversation("Test");
    REQUIRE(conversation.has_value());
    const auto id = conversation->id;

    REQUIRE(store.append_message(id, core::UserMessage{.content = "bonjour"}).has_value());
    REQUIRE(store.append_message(id, core::AssistantMessage{.text = "salut", .tool_calls = {}})
                .has_value());
    REQUIRE(store.append_message(id, core::UserMessage{.content = "sans réponse"}).has_value());
    REQUIRE(store.remove_last_message(id).has_value());

    const auto messages = store.messages(id);
    REQUIRE(messages.has_value());
    REQUIRE(messages->size() == 2);
    CHECK(std::get<core::UserMessage>(messages->at(0)).content == "bonjour");
    CHECK(std::get<core::AssistantMessage>(messages->at(1)).text == "salut");

    CHECK(count_of(store.conversation(id)) == 2);
}

TEST_CASE("effacer les messages garde la conversation et ne touche pas aux autres", "[store]") {
    auto store = memory_store();
    const auto cleared = store.create_conversation("À effacer");
    const auto other = store.create_conversation("Autre");
    REQUIRE(cleared.has_value());
    REQUIRE(other.has_value());
    REQUIRE(store.append_message(cleared->id, core::UserMessage{.content = "a"}).has_value());
    REQUIRE(store.append_message(cleared->id, core::UserMessage{.content = "b"}).has_value());
    REQUIRE(store.append_message(other->id, core::UserMessage{.content = "c"}).has_value());

    REQUIRE(store.clear_messages(cleared->id).has_value());
    CHECK(count_of(store.conversation(cleared->id)) == 0);
    CHECK(count_of(store.conversation(other->id)) == 1);
}

TEST_CASE("la conversation la plus récente est celle qui a reçu un message", "[store]") {
    auto store = memory_store();
    const auto older = store.create_conversation("Ancienne");
    const auto newer = store.create_conversation("Récente");
    REQUIRE(older.has_value());
    REQUIRE(newer.has_value());
    REQUIRE(store.append_message(older->id, core::UserMessage{.content = "réveil"}).has_value());

    CHECK(id_of(store.most_recent()) == older->id);
}

TEST_CASE("supprimer une conversation supprime ses messages (cascade)", "[store]") {
    auto store = memory_store();
    const auto conversation = store.create_conversation("À supprimer");
    REQUIRE(conversation.has_value());
    REQUIRE(store.append_message(conversation->id, core::UserMessage{.content = "x"}).has_value());
    REQUIRE(store.remove(conversation->id).has_value());
    const auto messages = store.messages(conversation->id);
    REQUIRE(messages.has_value());
    CHECK(messages->empty());
}

TEST_CASE("titres et messages piégés : aucune injection SQL", "[store]") {
    auto store = memory_store();
    const std::string trap = "x'); DROP TABLE conversations; --";
    const auto conversation = store.create_conversation(trap);
    REQUIRE(conversation.has_value());
    CHECK(conversation->title == trap);
    REQUIRE(store.append_message(conversation->id, core::UserMessage{.content = trap}).has_value());
    REQUIRE(store.rename(conversation->id, "\"; DELETE FROM messages; --").has_value());

    const auto list = store.list_conversations();
    REQUIRE(list.has_value());
    CHECK(list->size() == 1);
    const auto messages = store.messages(conversation->id);
    REQUIRE(messages.has_value());
    CHECK(messages->size() == 1);
}

TEST_CASE("les données survivent à la fermeture (fichier)", "[store]") {
    const auto path = std::filesystem::temp_directory_path() / "avenoir_store_test.db";
    std::filesystem::remove(path);
    std::int64_t id = 0;
    {
        auto store = storage::Store::open(path);
        REQUIRE(store.has_value());
        const auto conversation = store->create_conversation("Persistante");
        REQUIRE(conversation.has_value());
        id = conversation->id;
        REQUIRE(
            store->append_message(id, core::UserMessage{.content = "souviens-toi"}).has_value());
    }
    {
        // Réouverture : les migrations déjà appliquées ne refont rien.
        auto store = storage::Store::open(path);
        REQUIRE(store.has_value());
        const auto messages = store->messages(id);
        REQUIRE(messages.has_value());
        REQUIRE(messages->size() == 1);
        CHECK(std::get<core::UserMessage>(messages->front()).content == "souviens-toi");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
}

TEST_CASE("base vide : aucune conversation récente", "[store]") {
    const auto store = memory_store();
    const auto recent = store.most_recent();
    REQUIRE(recent.has_value());
    CHECK_FALSE(recent->has_value());
}

TEST_CASE("choix de risque des outils de modules", "[store]") {
    auto store = memory_store();
    auto choices = store.risk_choices();
    REQUIRE(choices.has_value());
    CHECK(choices->empty());

    REQUIRE(store.set_risk_choice("meteo__set_city", core::RiskLevel::Sensitive, false));
    REQUIRE(store.set_risk_choice("notes__delete", core::RiskLevel::Reversible, true));
    // Un nouveau choix remplace l'ancien.
    REQUIRE(store.set_risk_choice("meteo__set_city", core::RiskLevel::ReadOnly, true));

    choices = store.risk_choices();
    REQUIRE(choices.has_value());
    REQUIRE(choices->size() == 2);
    CHECK((*choices)[0].tool == "meteo__set_city");
    CHECK((*choices)[0].risk == core::RiskLevel::ReadOnly);
    CHECK((*choices)[0].agreed);
    CHECK_FALSE((*choices)[0].chosen_at.empty());
    CHECK((*choices)[1].risk == core::RiskLevel::Reversible);

    REQUIRE(store.remove_risk_choice("meteo__set_city"));
    choices = store.risk_choices();
    REQUIRE(choices.has_value());
    CHECK(choices->size() == 1);
}

TEST_CASE("état gardé d'un lancement à l'autre", "[store]") {
    auto store = memory_store();
    const auto missing = store.state("llm.active");
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->has_value());

    REQUIRE(store.set_state("llm.active", "leger").has_value());
    REQUIRE(store.set_state("llm.active", "claude").has_value()); // remplacé
    REQUIRE(store.set_state("autre", "x'); DROP TABLE app_state; --").has_value());
    const auto active = store.state("llm.active");
    REQUIRE(active.has_value());
    CHECK(active->value_or("") == "claude");
    CHECK(store.state("autre")->value_or("") == "x'); DROP TABLE app_state; --");
}
