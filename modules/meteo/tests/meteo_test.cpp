// Tests du module météo, sans réseau : réponses d'Open-Meteo enregistrées.

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/router.hpp"
#include "avenoir/modules/manifest.hpp"

#include "city_store.hpp"
#include "weather.hpp"

using namespace avenoir;
using namespace avenoir::meteo;
using nlohmann::json;

namespace {

Place lyon() {
    return {.name = "Lyon",
            .region = "Auvergne-Rhône-Alpes",
            .country = "France",
            .country_code = "FR",
            .latitude = 45.75,
            .longitude = 4.85};
}

Place brest() {
    return {.name = "Brest",
            .region = "Bretagne",
            .country = "France",
            .country_code = "FR",
            .latitude = 48.39,
            .longitude = -4.49};
}

/// Pays et région du lieu choisi (vides si aucun), pour comparer simplement.
std::string chosen(const std::optional<Place>& place) {
    return place ? place->country + " / " + place->region : std::string{};
}

/// Dossier temporaire supprimé à la fin du test.
class TempDir {
  public:
    TempDir() {
        std::random_device random;
        path_ =
            std::filesystem::temp_directory_path() / ("avenoir-meteo-" + std::to_string(random()));
        std::filesystem::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("validation des noms de ville", "[meteo]") {
    CHECK(validate_city("  Lyon ") == "Lyon");
    CHECK(validate_city("Saint-Étienne").has_value());
    CHECK(validate_city("L'Haÿ-les-Roses").has_value());
    CHECK(validate_city("Rio de Janeiro").has_value());
    CHECK(validate_city("東京").has_value());

    CHECK_FALSE(validate_city("").has_value());
    CHECK_FALSE(validate_city("   ").has_value());
    CHECK_FALSE(validate_city("Lyon; rm -rf ~").has_value());
    CHECK_FALSE(validate_city("Lyon&count=100").has_value());
    CHECK_FALSE(validate_city("../../etc/passwd").has_value());
    CHECK_FALSE(validate_city("Lyon\x1b[2J").has_value());
    CHECK_FALSE(validate_city("Lyon\nParis").has_value());
    CHECK_FALSE(validate_city(std::string(101, 'a')).has_value());
}

TEST_CASE("géocodage et choix du lieu", "[meteo]") {
    const json found = json::parse(R"({
        "results": [
            {"id": 629634, "name": "Brest", "latitude": 52.09755, "longitude": 23.68775,
             "country": "Biélorussie", "country_code": "BY", "admin1": "Brest"},
            {"id": 3030300, "name": "Brest", "latitude": 48.39029, "longitude": -4.48628,
             "country": "France", "country_code": "FR", "admin1": "Bretagne"}
        ],
        "generationtime_ms": 0.5
    })");
    const auto places = parse_geocoding(found);
    REQUIRE(places.has_value());
    REQUIRE(places->size() == 2);
    CHECK((*places)[1].country_code == "FR");

    // Sans indication : le premier (le plus peuplé).
    CHECK(chosen(choose_place(*places, "", "")) == "Biélorussie / Brest");
    // Pays précisé, par son nom ou son code, sans casse.
    CHECK(chosen(choose_place(*places, "france", "")) == "France / Bretagne");
    CHECK(chosen(choose_place(*places, "FR", "")) == "France / Bretagne");
    CHECK(chosen(choose_place(*places, "Japon", "")).empty());
    // Pays de la ville par défaut préféré.
    CHECK(chosen(choose_place(*places, "", "FR")) == "France / Bretagne");
    CHECK(chosen(choose_place(*places, "", "JP")) == "Biélorussie / Brest");

    CHECK(split_city("Brest, France").name == "Brest");
    CHECK(split_city("Brest, France").country == "France");
    CHECK(split_city("Brest").country.empty());
    CHECK(split_city(", France").name == ", France");

    const auto none = parse_geocoding(json::parse(R"({"generationtime_ms": 0.3})"));
    REQUIRE(none.has_value());
    CHECK(none->empty());
    CHECK_FALSE(parse_geocoding(json::parse(R"({"results": [{"name": "X"}]})")).has_value());
    CHECK_FALSE(parse_geocoding(json::array()).has_value());
}

TEST_CASE("prévisions et phrases en français", "[meteo]") {
    const json response = json::parse(R"({
        "latitude": 45.76, "longitude": 4.84, "timezone": "Europe/Paris",
        "current": {"time": "2026-11-07T15:00", "interval": 900, "temperature_2m": 13.6,
                    "apparent_temperature": 11.8, "relative_humidity_2m": 71,
                    "weather_code": 3, "wind_speed_10m": 12.4, "precipitation": 0.0},
        "daily": {"time": ["2026-11-07", "2026-11-08", "2026-11-09"],
                  "weather_code": [3, 61, 0],
                  "temperature_2m_max": [15.2, 12.9, 11.0],
                  "temperature_2m_min": [7.9, 8.4, -0.4],
                  "precipitation_probability_max": [10, 85, 0],
                  "precipitation_sum": [0.0, 4.2, 0.0]}
    })");
    const auto forecast = parse_forecast(response);
    REQUIRE(forecast.has_value());
    const std::optional<Current>& current = forecast->current;
    REQUIRE(current.has_value());
    REQUIRE(forecast->days.size() == 3);
    if (!current) {
        return; // Pour l'analyse statique (REQUIRE a déjà échoué).
    }

    CHECK(format_current(lyon(), *current) ==
          "À Lyon (Auvergne-Rhône-Alpes, France) : 14\u00a0°C, ressenti 12\u00a0°C, couvert. "
          "Vent 12\u00a0km/h, humidité 71\u00a0%.");
    CHECK(format_forecast(lyon(), forecast->days) ==
          "Prévisions pour Lyon (Auvergne-Rhône-Alpes, France) :\n"
          "- Aujourd'hui : couvert, 8\u00a0°C à 15\u00a0°C, pluie 10\u00a0%\n"
          "- Demain : pluie faible, 8\u00a0°C à 13\u00a0°C, pluie 85\u00a0% (4,2\u00a0mm)\n"
          "- Lundi 9 : ciel dégagé, 0\u00a0°C à 11\u00a0°C, pluie 0\u00a0%");

    CHECK_FALSE(parse_forecast(json::parse(R"({"current": {"weather_code": 1}})")).has_value());
    CHECK_FALSE(parse_forecast(json::parse(R"({"daily": {"time": ["2026-11-07"]}})")).has_value());
}

TEST_CASE("codes météo WMO", "[meteo]") {
    CHECK(describe_code(0) == "ciel dégagé");
    CHECK(describe_code(81) == "averses");
    CHECK(describe_code(99) == "orage avec grêle");
    CHECK(describe_code(42) == "temps indéterminé");
}

TEST_CASE("ville par défaut et annulation sûre", "[meteo]") {
    const TempDir temp;
    const CityStore store(temp.path());
    CHECK_FALSE(store.load().has_value());

    const auto first = store.change(lyon());
    REQUIRE(first.has_value());
    CHECK(store.load() == lyon());

    const auto second = store.change(brest());
    REQUIRE(second.has_value());
    CHECK(store.load() == brest());

    // L'ancien changement ne s'annule pas : la ville a changé depuis.
    const auto stale = store.undo(*first);
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().contains("a changé depuis"));
    CHECK(store.load() == brest());

    // Le dernier, oui : retour à Lyon, puis à aucune ville.
    const auto back = store.undo(*second);
    REQUIRE(back.has_value());
    CHECK(*back == "Ville par défaut remise à Lyon (Auvergne-Rhône-Alpes, France).");
    CHECK(store.load() == lyon());
    const auto cleared = store.undo(*first);
    REQUIRE(cleared.has_value());
    CHECK(*cleared == "Ville par défaut effacée.");
    CHECK_FALSE(store.load().has_value());

    CHECK_FALSE(store.undo("pas du json").has_value());
    CHECK_FALSE(CityStore(std::nullopt).change(lyon()).has_value());
}

TEST_CASE("manifeste et voie rapide du module météo", "[meteo]") {
    const std::filesystem::path manifest_path(AVENOIR_METEO_MANIFEST);
    const auto manifest = modules::load_manifest(manifest_path.parent_path());
    REQUIRE(manifest.has_value());
    CHECK(manifest->id == "meteo");
    CHECK(manifest->permissions == std::vector<std::string>{"network"});

    core::FastRouter router;
    for (const auto& rule : manifest->fast_path) {
        REQUIRE(router
                    .add_rule(rule.pattern, modules::qualified_tool_name("meteo", rule.tool),
                              rule.arg, rule.arguments)
                    .has_value());
    }
    const auto route = [&router](const char* text) { return router.route(text); };

    CHECK(route("Quel temps fait-il ?") ==
          core::FastRoute{.tool = "meteo__current", .arguments = json::object()});
    CHECK(route("météo") == core::FastRoute{.tool = "meteo__current", .arguments = json::object()});
    CHECK(route("quel temps fait-il à Saint-Étienne ?") ==
          core::FastRoute{.tool = "meteo__current", .arguments = {{"city", "Saint-Étienne"}}});
    CHECK(route("Va-t-il pleuvoir demain à Lyon ?") ==
          core::FastRoute{.tool = "meteo__forecast", .arguments = {{"days", 2}, {"city", "Lyon"}}});
    CHECK(route("est-ce qu'il va pleuvoir demain") ==
          core::FastRoute{.tool = "meteo__forecast", .arguments = {{"days", 2}}});
    CHECK(route("est-ce qu’il va pleuvoir demain ?") ==
          core::FastRoute{.tool = "meteo__forecast", .arguments = {{"days", 2}}});
    CHECK(
        route("prévisions météo pour Brest") ==
        core::FastRoute{.tool = "meteo__forecast", .arguments = {{"days", 7}, {"city", "Brest"}}});
    // Phrases plus riches : au LLM.
    CHECK_FALSE(route("quel temps fera-t-il ce week-end à la montagne ?").has_value());
    CHECK_FALSE(route("dois-je prendre un parapluie ?").has_value());
}
