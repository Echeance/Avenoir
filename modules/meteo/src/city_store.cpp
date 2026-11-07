#include "city_store.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace avenoir::meteo {

namespace {

using nlohmann::json;

json optional_place(const std::optional<Place>& place) {
    return place ? place_to_json(*place) : json(nullptr);
}

} // namespace

CityStore::CityStore(std::optional<std::filesystem::path> directory) {
    if (directory) {
        file_ = *directory / "ville.json";
    }
}

std::optional<Place> CityStore::load() const {
    if (!file_) {
        return std::nullopt;
    }
    std::ifstream in(*file_, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), {});
    return place_from_json(json::parse(content, nullptr, /*allow_exceptions=*/false));
}

Result<void> CityStore::save(const std::optional<Place>& place) const {
    if (!file_) {
        return std::unexpected("pas de dossier de données : la ville ne peut pas être retenue");
    }
    std::error_code error;
    if (!place) {
        std::filesystem::remove(*file_, error);
        if (error) {
            return std::unexpected("effacement impossible : " + error.message());
        }
        return {};
    }
    const std::filesystem::path temporary = file_->string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << place_to_json(*place).dump(2);
        if (!out) {
            return std::unexpected("écriture impossible : " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, *file_, error);
    if (error) {
        return std::unexpected("écriture impossible : " + error.message());
    }
    return {};
}

Result<std::string> CityStore::change(const Place& place) const {
    const std::optional<Place> previous = load();
    if (auto saved = save(place); !saved) {
        return std::unexpected(std::move(saved.error()));
    }
    return json{{"previous", optional_place(previous)}, {"expected", place_to_json(place)}}.dump();
}

Result<std::string> CityStore::undo(std::string_view token) const {
    const json parsed = json::parse(token, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object() || !parsed.contains("expected") || !parsed.contains("previous")) {
        return std::unexpected("jeton d'annulation invalide");
    }
    // L'état ne doit pas avoir changé depuis : sinon, on n'écrase rien.
    const std::optional<Place> expected = place_from_json(parsed["expected"]);
    if (!expected || load() != expected) {
        return std::unexpected("la ville par défaut a changé depuis : rien n'a été annulé");
    }
    const std::optional<Place> previous = place_from_json(parsed["previous"]);
    if (auto saved = save(previous); !saved) {
        return std::unexpected(std::move(saved.error()));
    }
    return previous ? "Ville par défaut remise à " + place_label(*previous) + "."
                    : std::string("Ville par défaut effacée.");
}

} // namespace avenoir::meteo
