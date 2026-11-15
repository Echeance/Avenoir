#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/modules/tabs.hpp"

using namespace avenoir;
using nlohmann::json;

namespace {

modules::Manifest manifest() {
    modules::Manifest out;
    out.id = "meteo";
    out.commands.push_back(modules::CommandDeclaration{
        .name = "meteo", .tool = "current", .arg = "city", .description = ""});
    return out;
}

} // namespace

TEST_CASE("onglets : composants connus gardés, le reste écarté", "[tabs]") {
    const json result{
        {"components",
         {{{"type", "heading"}, {"text", "Lyon"}},
          {{"type", "text"}, {"text", "couvert"}, {"tone", "warning"}},
          {{"type", "stat"}, {"label", "Température"}, {"value", "7 °C"}},
          {{"type", "list"}, {"items", {"a", 3, json::object()}}},
          {{"type", "table"}, {"columns", {"Jour", "Max"}}, {"rows", {{"lun.", "9"}, {"mar."}}}},
          {{"type", "button"}, {"label", "Actualiser"}, {"command", "meteo"}, {"argument", "Lyon"}},
          {{"type", "script"}, {"code", "alert(1)"}},
          {{"type", "html"}, {"text", "<b>x</b>"}},
          "pas un objet"}}};
    const auto tab = modules::validate_tab(result, manifest());
    REQUIRE(tab.has_value());
    const json& components = (*tab)["components"];
    REQUIRE(components.size() == 6);
    CHECK(components[2] == json{{"type", "stat"}, {"label", "Température"}, {"value", "7 °C"}});
    CHECK(components[3]["items"] == json{"a", "3", ""});
    CHECK(components[4]["rows"][1] == json{"mar.", ""}); // ligne complétée
    CHECK(components[5]["command"] == "meteo");
}

TEST_CASE("onglets : un bouton ne lance qu'une commande du module", "[tabs]") {
    const json result{{"components",
                       {{{"type", "button"}, {"label", "Éteindre"}, {"command", "eteindre"}},
                        {{"type", "button"}, {"label", "Réglage"}, {"command", "reglage"}}}}};
    const auto tab = modules::validate_tab(result, manifest());
    REQUIRE(tab.has_value());
    CHECK((*tab)["components"].empty());
}

TEST_CASE("onglets : textes nettoyés et bornés, nombre de composants borné", "[tabs]") {
    json many = json::array();
    for (int i = 0; i < 300; ++i) {
        many.push_back({{"type", "text"}, {"text", "x"}});
    }
    CHECK(modules::validate_tab({{"components", many}}, manifest())
              .value_or(json{})["components"]
              .size() == modules::kMaxTabComponents);

    // ESC, U+202E (forçage droite à gauche), BEL, U+2066 (isolat), écrits
    // octet par octet : jamais tels quels dans le source.
    const std::string override_mark{static_cast<char>(0xE2), static_cast<char>(0x80),
                                    static_cast<char>(0xAE)};
    const std::string isolate_mark{static_cast<char>(0xE2), static_cast<char>(0x81),
                                   static_cast<char>(0xA6)};
    const std::string tricky_text = "a\x1b[31mb" + override_mark +
                                    "c\x07"
                                    "d" +
                                    isolate_mark + "e";
    const json tricky{{"components",
                       {{{"type", "text"}, {"text", tricky_text}},
                        {{"type", "heading"}, {"text", std::string(2000, 'z')}},
                        {{"type", "text"}, {"text", "\x01\x02"}}}}};
    const auto tab = modules::validate_tab(tricky, manifest());
    REQUIRE(tab.has_value());
    REQUIRE((*tab)["components"].size() == 2); // texte vide après nettoyage : écarté
    CHECK((*tab)["components"][0]["text"] == "a[31mbcde");
    CHECK((*tab)["components"][1]["text"].get<std::string>().size() == modules::kMaxTabText);
}

TEST_CASE("onglets : résultat mal formé refusé", "[tabs]") {
    CHECK_FALSE(modules::validate_tab(json::array(), manifest()).has_value());
    CHECK_FALSE(modules::validate_tab({{"components", "x"}}, manifest()).has_value());
}
