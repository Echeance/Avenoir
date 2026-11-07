#include "weather.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace avenoir::meteo {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxCityBytes = 100;
/// Espace insécable entre un nombre et son unité (pas de retour à la ligne).
constexpr std::string_view kNbsp = "\u00a0";

std::string_view trim(std::string_view text) {
    const auto space = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// Nombre à virgule (« 4,2 »).
std::string decimal(double value) {
    std::string text = std::format("{:.1f}", value);
    std::ranges::replace(text, '.', ',');
    return text;
}

/// Température arrondie, sans « -0 ».
std::string degrees(double value) {
    const long rounded = std::lround(value);
    return std::format("{}{}°C", rounded == 0 ? 0L : rounded, kNbsp);
}

std::optional<double> number(const json& object, std::string_view key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<double>();
}

std::string text(const json& object, std::string_view key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

/// Élément `index` d'un tableau de nombres de `daily`, s'il existe.
std::optional<double> daily_number(const json& daily, std::string_view key, std::size_t index) {
    const auto it = daily.find(key);
    if (it == daily.end() || !it->is_array() || index >= it->size() || !(*it)[index].is_number()) {
        return std::nullopt;
    }
    return (*it)[index].get<double>();
}

/// « Aujourd'hui », « Demain », puis « Samedi 9 ».
std::string day_label(std::size_t index, std::string_view date) {
    if (index == 0) {
        return "Aujourd'hui";
    }
    if (index == 1) {
        return "Demain";
    }
    // « AAAA-MM-JJ », chiffres seulement.
    const auto field = [date](std::size_t position, std::size_t length) -> std::optional<int> {
        int value = 0;
        for (const char c : date.substr(position, length)) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = (value * 10) + (c - '0');
        }
        return value;
    };
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return std::string(date);
    }
    const auto year = field(0, 4);
    const auto month = field(5, 2);
    const auto day = field(8, 2);
    if (!year || !month || !day) {
        return std::string(date);
    }
    const std::chrono::year_month_day ymd{std::chrono::year{*year},
                                          std::chrono::month{static_cast<unsigned>(*month)},
                                          std::chrono::day{static_cast<unsigned>(*day)}};
    if (!ymd.ok()) {
        return std::string(date);
    }
    static constexpr std::array<std::string_view, 7> kNames{
        "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
    const auto weekday = std::chrono::weekday{std::chrono::sys_days{ymd}};
    return std::format("{} {}", kNames.at(weekday.c_encoding()), *day);
}

} // namespace

Result<std::string> validate_city(std::string_view city) {
    city = trim(city);
    if (city.empty()) {
        return std::unexpected("nom de ville vide");
    }
    if (city.size() > kMaxCityBytes) {
        return std::unexpected("nom de ville trop long");
    }
    constexpr std::string_view kForbidden = "<>{}[]|\\^`\"$;=&?#%*/@!~+";
    for (const char c : city) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F || kForbidden.contains(c)) {
            return std::unexpected("nom de ville invalide : « " + std::string(city) + " »");
        }
    }
    return std::string(city);
}

CityQuery split_city(std::string_view city) {
    const auto comma = city.rfind(',');
    if (comma == std::string_view::npos) {
        return {.name = std::string(trim(city)), .country = {}};
    }
    const std::string_view name = trim(city.substr(0, comma));
    const std::string_view country = trim(city.substr(comma + 1));
    if (name.empty() || country.empty()) {
        return {.name = std::string(trim(city)), .country = {}};
    }
    return {.name = std::string(name), .country = std::string(country)};
}

Result<std::vector<Place>> parse_geocoding(const nlohmann::json& response) {
    if (!response.is_object()) {
        return std::unexpected("réponse du géocodage illisible");
    }
    std::vector<Place> places;
    const auto results = response.find("results");
    if (results == response.end() || !results->is_array()) {
        return places;
    }
    for (const json& item : *results) {
        const auto latitude = item.is_object() ? number(item, "latitude") : std::nullopt;
        const auto longitude = item.is_object() ? number(item, "longitude") : std::nullopt;
        if (!latitude || !longitude || text(item, "name").empty()) {
            return std::unexpected("réponse du géocodage incomplète");
        }
        places.push_back(Place{.name = text(item, "name"),
                               .region = text(item, "admin1"),
                               .country = text(item, "country"),
                               .country_code = text(item, "country_code"),
                               .latitude = *latitude,
                               .longitude = *longitude});
    }
    return places;
}

std::optional<Place> choose_place(std::span<const Place> places, std::string_view country,
                                  std::string_view preferred_code) {
    // Comparaison sans casse (ASCII) : « france », « France », « FR ».
    const auto same = [](std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](char x, char y) {
            const auto lower = [](char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            };
            return lower(x) == lower(y);
        });
    };
    if (!country.empty()) {
        const auto it = std::ranges::find_if(places, [&](const Place& place) {
            return same(place.country, country) || same(place.country_code, country);
        });
        return it == places.end() ? std::nullopt : std::optional<Place>(*it);
    }
    if (!preferred_code.empty()) {
        const auto it = std::ranges::find_if(
            places, [&](const Place& place) { return same(place.country_code, preferred_code); });
        if (it != places.end()) {
            return *it;
        }
    }
    return places.empty() ? std::nullopt : std::optional<Place>(places.front());
}

Result<Forecast> parse_forecast(const nlohmann::json& response) {
    if (!response.is_object()) {
        return std::unexpected("réponse météo illisible");
    }
    Forecast forecast;
    if (const auto current = response.find("current");
        current != response.end() && current->is_object()) {
        const auto temperature = number(*current, "temperature_2m");
        const auto code = number(*current, "weather_code");
        if (!temperature || !code) {
            return std::unexpected("réponse météo incomplète");
        }
        forecast.current = Current{
            .temperature = *temperature,
            .apparent = number(*current, "apparent_temperature").value_or(*temperature),
            .wind = number(*current, "wind_speed_10m").value_or(0),
            .precipitation = number(*current, "precipitation").value_or(0),
            .humidity =
                static_cast<int>(std::lround(number(*current, "relative_humidity_2m").value_or(0))),
            .code = static_cast<int>(*code),
        };
    }
    if (const auto daily = response.find("daily"); daily != response.end() && daily->is_object()) {
        const auto dates = daily->find("time");
        if (dates == daily->end() || !dates->is_array()) {
            return std::unexpected("réponse météo incomplète");
        }
        for (std::size_t i = 0; i < dates->size() && i < kMaxDays; ++i) {
            const auto code = daily_number(*daily, "weather_code", i);
            const auto minimum = daily_number(*daily, "temperature_2m_min", i);
            const auto maximum = daily_number(*daily, "temperature_2m_max", i);
            if (!(*dates)[i].is_string() || !code || !minimum || !maximum) {
                return std::unexpected("réponse météo incomplète");
            }
            const auto probability = daily_number(*daily, "precipitation_probability_max", i);
            forecast.days.push_back(Day{
                .date = (*dates)[i].get<std::string>(),
                .code = static_cast<int>(*code),
                .minimum = *minimum,
                .maximum = *maximum,
                .rain_probability =
                    probability ? std::optional<int>(static_cast<int>(std::lround(*probability)))
                                : std::nullopt,
                .rain = daily_number(*daily, "precipitation_sum", i).value_or(0),
            });
        }
    }
    return forecast;
}

std::string_view describe_code(int code) {
    switch (code) {
    case 0:
        return "ciel dégagé";
    case 1:
        return "plutôt dégagé";
    case 2:
        return "partiellement nuageux";
    case 3:
        return "couvert";
    case 45:
        return "brouillard";
    case 48:
        return "brouillard givrant";
    case 51:
        return "bruine légère";
    case 53:
        return "bruine";
    case 55:
        return "bruine dense";
    case 56:
    case 57:
        return "bruine verglaçante";
    case 61:
        return "pluie faible";
    case 63:
        return "pluie";
    case 65:
        return "forte pluie";
    case 66:
    case 67:
        return "pluie verglaçante";
    case 71:
        return "neige faible";
    case 73:
        return "neige";
    case 75:
        return "forte neige";
    case 77:
        return "grains de neige";
    case 80:
        return "averses faibles";
    case 81:
        return "averses";
    case 82:
        return "violentes averses";
    case 85:
        return "averses de neige";
    case 86:
        return "fortes averses de neige";
    case 95:
        return "orage";
    case 96:
    case 99:
        return "orage avec grêle";
    default:
        return "temps indéterminé";
    }
}

std::string place_label(const Place& place) {
    std::string details;
    if (!place.region.empty() && place.region != place.name) {
        details = place.region;
    }
    if (!place.country.empty()) {
        details += (details.empty() ? "" : ", ") + place.country;
    }
    return details.empty() ? place.name : std::format("{} ({})", place.name, details);
}

std::string format_current(const Place& place, const Current& current) {
    std::string out = std::format("À {} : {}", place_label(place), degrees(current.temperature));
    if (std::lround(current.apparent) != std::lround(current.temperature)) {
        out += ", ressenti " + degrees(current.apparent);
    }
    out += std::format(", {}. Vent {}{}km/h, humidité {}{}%", describe_code(current.code),
                       std::lround(current.wind), kNbsp, current.humidity, kNbsp);
    if (current.precipitation >= 0.1) {
        out += std::format(", précipitations {}{}mm", decimal(current.precipitation), kNbsp);
    }
    return out + ".";
}

std::string format_forecast(const Place& place, std::span<const Day> days) {
    std::string out = "Prévisions pour " + place_label(place) + " :";
    for (std::size_t i = 0; i < days.size(); ++i) {
        const Day& day = days[i];
        out += std::format("\n- {} : {}, {} à {}", day_label(i, day.date), describe_code(day.code),
                           degrees(day.minimum), degrees(day.maximum));
        if (day.rain_probability) {
            out += std::format(", pluie {}{}%", *day.rain_probability, kNbsp);
        }
        if (day.rain >= 0.1) {
            out += std::format(" ({}{}mm)", decimal(day.rain), kNbsp);
        }
    }
    return out;
}

nlohmann::json place_to_json(const Place& place) {
    return {{"name", place.name},         {"region", place.region},
            {"country", place.country},   {"country_code", place.country_code},
            {"latitude", place.latitude}, {"longitude", place.longitude}};
}

std::optional<Place> place_from_json(const nlohmann::json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    const auto latitude = number(object, "latitude");
    const auto longitude = number(object, "longitude");
    if (!latitude || !longitude || text(object, "name").empty()) {
        return std::nullopt;
    }
    return Place{.name = text(object, "name"),
                 .region = text(object, "region"),
                 .country = text(object, "country"),
                 .country_code = text(object, "country_code"),
                 .latitude = *latitude,
                 .longitude = *longitude};
}

} // namespace avenoir::meteo
