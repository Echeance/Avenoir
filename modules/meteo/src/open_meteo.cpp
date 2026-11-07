#include "open_meteo.hpp"

#include <format>
#include <string>
#include <string_view>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace avenoir::meteo {

namespace {

constexpr std::string_view kGeocodingUrl = "https://geocoding-api.open-meteo.com/v1/search";
constexpr std::string_view kForecastUrl = "https://api.open-meteo.com/v1/forecast";
constexpr std::int32_t kTimeoutMs = 10'000;

/// Requête GET ; seules ces deux adresses sont jamais contactées.
Result<nlohmann::json> get(std::string_view url, cpr::Parameters parameters) {
    const cpr::Response response =
        cpr::Get(cpr::Url{std::string(url)}, std::move(parameters), cpr::Timeout{kTimeoutMs},
                 cpr::UserAgent{"Avenoir-meteo/1.0"});
    if (response.error.code != cpr::ErrorCode::OK) {
        return std::unexpected("service météo injoignable (" + response.error.message + ")");
    }
    if (response.status_code != 200) {
        return std::unexpected(std::format("service météo : erreur HTTP {}", response.status_code));
    }
    nlohmann::json body = nlohmann::json::parse(response.text, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded()) {
        return std::unexpected("réponse du service météo illisible");
    }
    return body;
}

} // namespace

Result<Place> geocode(std::string_view city, std::string_view preferred_code) {
    const CityQuery query = split_city(city);
    auto response =
        get(kGeocodingUrl,
            cpr::Parameters{
                {"name", query.name}, {"count", "10"}, {"language", "fr"}, {"format", "json"}});
    if (!response) {
        return std::unexpected(std::move(response.error()));
    }
    auto places = parse_geocoding(*response);
    if (!places) {
        return std::unexpected(std::move(places.error()));
    }
    auto place = choose_place(*places, query.country, preferred_code);
    if (!place) {
        return std::unexpected("ville introuvable : « " + std::string(city) + " »");
    }
    return std::move(*place);
}

Result<Forecast> fetch_forecast(const Place& place, int days) {
    auto response = get(kForecastUrl,
                        cpr::Parameters{
                            {"latitude", std::format("{:.4f}", place.latitude)},
                            {"longitude", std::format("{:.4f}", place.longitude)},
                            {"current", "temperature_2m,apparent_temperature,relative_humidity_2m,"
                                        "weather_code,wind_speed_10m,precipitation"},
                            {"daily", "weather_code,temperature_2m_max,temperature_2m_min,"
                                      "precipitation_probability_max,precipitation_sum"},
                            {"timezone", "auto"},
                            {"forecast_days", std::to_string(days)},
                        });
    if (!response) {
        return std::unexpected(std::move(response.error()));
    }
    return parse_forecast(*response);
}

} // namespace avenoir::meteo
