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

TEST_CASE("onglets web : données bornées, noms d'événements stricts", "[tabs]") {
    const auto kept = modules::validate_web_data(json{{"data", {{"count", 3}}}}, manifest());
    REQUIRE(kept.has_value());
    CHECK(kept.value_or(json{}) == json{{"data", {{"count", 3}}}});
    CHECK(modules::validate_web_data(json::object(), manifest()).value_or(json{}) ==
          json{{"data", nullptr}});
    CHECK_FALSE(modules::validate_web_data(json::array(), manifest()).has_value());
    CHECK_FALSE(modules::validate_web_data(
                    json{{"data", std::string(modules::kMaxWebTabData, 'x')}}, manifest())
                    .has_value());

    CHECK(modules::is_tab_event_name("refresh"));
    CHECK(modules::is_tab_event_name("set_city-2"));
    CHECK_FALSE(modules::is_tab_event_name(""));
    CHECK_FALSE(modules::is_tab_event_name("Refresh"));
    CHECK_FALSE(modules::is_tab_event_name("a b"));
    CHECK_FALSE(modules::is_tab_event_name("../x"));
    CHECK_FALSE(modules::is_tab_event_name(std::string(65, 'a')));
}

TEST_CASE("onglets : images en data: matricielles seulement, bornées", "[tabs]") {
    const std::string png = "data:image/png;base64,iVBORw0KGgo=";
    const json result{
        {"components",
         {{{"type", "image"}, {"src", png}, {"alt", "carte"}},
          {{"type", "image"}, {"src", "data:image/svg+xml;base64,PHN2Zz4="}},
          {{"type", "image"}, {"src", "https://example.com/a.png"}},
          {{"type", "image"}, {"src", "data:image/png;base64,<script>"}},
          {{"type", "image"}, {"src", "data:image/png;base64,"}},
          {{"type", "image"},
           {"src", "data:image/png;base64," + std::string(modules::kMaxTabImage, 'A')}}}}};
    const auto tab = modules::validate_tab(result, manifest());
    REQUIRE(tab.has_value());
    const json components = tab.value_or(json{})["components"];
    REQUIRE(components.size() == 1);
    CHECK(components[0] == json{{"type", "image"}, {"src", png}, {"alt", "carte"}});

    json many = json::array();
    for (int i = 0; i < 6; ++i) {
        many.push_back({{"type", "image"}, {"src", png}});
    }
    CHECK(modules::validate_tab(json{{"components", many}}, manifest())
              .value_or(json{})["components"]
              .size() == modules::kMaxTabImages);
}

TEST_CASE("onglets : graphiques alignés sur les étiquettes", "[tabs]") {
    const json result{
        {"components",
         {{{"type", "chart"},
           {"kind", "line"},
           {"title", "Températures"},
           {"unit", "°C"},
           {"labels", {"lun.", "mar.", "mer."}},
           {"series",
            {{{"name", "max"}, {"values", {9, "x", 11.5, 99}}},
             {{"name", "min"}, {"values", {2}}},
             "pas une série"}}},
          {{"type", "chart"}, {"kind", "pie"}, {"labels", {"a"}}, {"series", json::array()}},
          {{"type", "chart"}, {"labels", json::array()}, {"series", json::array()}}}}};
    const json components =
        modules::validate_tab(result, manifest()).value_or(json{})["components"];
    REQUIRE(components.size() == 1);
    const json& chart = components[0];
    CHECK(chart["kind"] == "line");
    CHECK(chart["unit"] == "°C");
    REQUIRE(chart["series"].size() == 2);
    CHECK(chart["series"][0]["values"] == json{9.0, nullptr, 11.5});
    CHECK(chart["series"][1]["values"] == json{2.0, nullptr, nullptr});
}

TEST_CASE("onglets : un formulaire ne remplit qu'une commande du module", "[tabs]") {
    const json result{{"components",
                       {{{"type", "form"},
                         {"command", "meteo"},
                         {"label", "Ville"},
                         {"placeholder", "Lyon"},
                         {"options", {"Lyon", "Brest"}}},
                        {{"type", "form"}, {"command", "arreter"}, {"label", "Piège"}},
                        {{"type", "form"}, {"command", "meteo"}}}}};
    const json components =
        modules::validate_tab(result, manifest()).value_or(json{})["components"];
    REQUIRE(components.size() == 1);
    CHECK(components[0]["command"] == "meteo");
    CHECK(components[0]["submit"] == "Envoyer");
    CHECK(components[0]["options"] == json{"Lyon", "Brest"});
}
