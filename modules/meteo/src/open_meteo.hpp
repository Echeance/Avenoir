#pragma once

#include <string_view>

#include "weather.hpp"

namespace avenoir::meteo {

// Données : Open-Meteo.com (CC BY 4.0), sans clé d'API. Seuls le nom de la
// ville et ses coordonnées sont envoyés au service.

/// Cherche une ville (« Brest » ou « Brest, France ») ; à pays non précisé,
/// celui de `preferred_code` est préféré. Erreur lisible si elle est
/// introuvable ou si le service ne répond pas.
[[nodiscard]] Result<Place> geocode(std::string_view city, std::string_view preferred_code);

/// Conditions actuelles et prévisions sur `days` jours (1 à 7).
[[nodiscard]] Result<Forecast> fetch_forecast(const Place& place, int days);

} // namespace avenoir::meteo
