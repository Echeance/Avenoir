#pragma once

// Module météo : logique sans réseau (validation, lecture des réponses
// d'Open-Meteo, phrases en français). Testée dans tests/meteo_test.cpp.

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace avenoir::meteo {

/// Erreur lisible, renvoyée telle quelle à l'utilisateur.
template <typename T>
using Result = std::expected<T, std::string>;

/// Lieu trouvé par le géocodage.
struct Place {
    std::string name;
    std::string region; ///< Région administrative (peut être vide).
    std::string country;
    std::string country_code; ///< ISO 3166-1 alpha-2 (« FR »).
    double latitude = 0;
    double longitude = 0;

    bool operator==(const Place&) const = default;
};

struct Current {
    double temperature = 0;
    double apparent = 0;
    double wind = 0;          ///< km/h
    double precipitation = 0; ///< mm
    int humidity = 0;         ///< %
    int code = 0;             ///< Code météo WMO.
};

struct Day {
    std::string date; ///< AAAA-MM-JJ
    int code = 0;
    double minimum = 0;
    double maximum = 0;
    std::optional<int> rain_probability; ///< %
    double rain = 0;                     ///< mm
};

struct Forecast {
    std::optional<Current> current;
    std::vector<Day> days;
};

/// Nombre de jours de prévision : 1 à 7.
inline constexpr int kMaxDays = 7;
inline constexpr int kDefaultDays = 3;

/// Nom de ville saisi (ou fourni par le LLM) : espaces retirés autour, 1 à
/// 100 octets, sans caractère de contrôle ni symbole sans rapport avec un nom
/// de lieu. Erreur lisible sinon.
[[nodiscard]] Result<std::string> validate_city(std::string_view city);

/// Ville demandée : « Brest » ou « Brest, France » (pays après la virgule).
struct CityQuery {
    std::string name;
    std::string country; ///< Vide si non précisé.
};

/// Sépare le pays éventuel (après la dernière virgule) d'un nom validé.
[[nodiscard]] CityQuery split_city(std::string_view city);

/// Réponse de l'API de géocodage : lieux trouvés, du plus au moins pertinent.
[[nodiscard]] Result<std::vector<Place>> parse_geocoding(const nlohmann::json& response);

/// Choisit parmi les lieux trouvés : celui du pays demandé (nom ou code,
/// sans casse), sinon celui du pays préféré (`preferred_code`, celui de la
/// ville par défaut), sinon le premier. Rien si le pays demandé n'y est pas.
[[nodiscard]] std::optional<Place> choose_place(std::span<const Place> places,
                                                std::string_view country,
                                                std::string_view preferred_code);

/// Réponse de l'API de prévisions (`current` et `daily`).
[[nodiscard]] Result<Forecast> parse_forecast(const nlohmann::json& response);

/// « ciel dégagé », « averses »… d'après le code WMO.
[[nodiscard]] std::string_view describe_code(int code);

/// « Lyon (Auvergne-Rhône-Alpes, France) ».
[[nodiscard]] std::string place_label(const Place& place);

/// « À Lyon (France) : 14 °C, ressenti 12 °C, couvert. Vent 12 km/h, humidité 70 %. »
[[nodiscard]] std::string format_current(const Place& place, const Current& current);

/// Une ligne par jour : « Aujourd'hui », « Demain », puis « Samedi 9 ».
[[nodiscard]] std::string format_forecast(const Place& place, std::span<const Day> days);

[[nodiscard]] nlohmann::json place_to_json(const Place& place);
[[nodiscard]] std::optional<Place> place_from_json(const nlohmann::json& object);

} // namespace avenoir::meteo
