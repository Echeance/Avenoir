// Module météo d'Avenoir : conditions actuelles, prévisions et ville par
// défaut, d'après Open-Meteo (sans clé d'API).
//
// Protocole « avenoir » (MCP + annulation `avenoir/undo`) : JSON-RPC 2.0 sur
// l'entrée et la sortie standard, un message par ligne. Rien d'autre n'est
// écrit sur la sortie standard ; les messages de diagnostic vont sur stderr.

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "city_store.hpp"
#include "open_meteo.hpp"
#include "weather.hpp"

namespace {

using nlohmann::json;
using namespace avenoir::meteo;

constexpr std::string_view kVersion = "1.0.0";
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

void send(const json& message) {
    std::cout << message.dump(-1, ' ', false, json::error_handler_t::replace) << '\n' << std::flush;
}

json text_result(std::string text, bool is_error = false) {
    return {{"content", json::array({{{"type", "text"}, {"text", std::move(text)}}})},
            {"isError", is_error}};
}

json city_property(std::string_view description) {
    return {{"type", "string"}, {"description", description}};
}

json tool_list() {
    return json::array({
        {{"name", "current"},
         {"description", "Météo actuelle (température, ressenti, ciel, vent, humidité) dans une "
                         "ville, ou dans la ville par défaut de l'utilisateur si aucune n'est "
                         "donnée."},
         {"inputSchema",
          {{"type", "object"},
           {"properties",
            {{"city", city_property("Ville, ex. « Lyon », ou « Brest, France » pour préciser le "
                                    "pays. Facultatif : ville par défaut sinon.")}}},
           {"additionalProperties", false}}}},
        {{"name", "forecast"},
         {"description", "Prévisions jour par jour (ciel, températures minimale et maximale, "
                         "risque de pluie) à partir d'aujourd'hui. Pour « demain », demander "
                         "2 jours."},
         {"inputSchema",
          {{"type", "object"},
           {"properties",
            {{"city", city_property("Ville. Facultatif : ville par défaut sinon.")},
             {"days",
              {{"type", "integer"},
               {"minimum", 1},
               {"maximum", kMaxDays},
               {"description", "Nombre de jours, aujourd'hui compris (3 par défaut)."}}}}},
           {"additionalProperties", false}}}},
        {{"name", "set_city"},
         {"description", "Règle la ville par défaut de l'utilisateur pour la météo (par exemple "
                         "quand il dit « j'habite à … » ou « ma ville est … »)."},
         {"inputSchema",
          {{"type", "object"},
           {"properties",
            {{"city", city_property("Ville, ex. « Lyon », ou « Brest, France » pour "
                                    "préciser le pays.")}}},
           {"required", json::array({"city"})},
           {"additionalProperties", false}}}},
    });
}

class Meteo {
  public:
    explicit Meteo(std::optional<std::filesystem::path> data) : cities_(std::move(data)) {}

    [[nodiscard]] json call(const std::string& name, const json& arguments) const {
        if (!arguments.is_object()) {
            return text_result("arguments invalides", true);
        }
        if (name == "current") {
            return current(arguments);
        }
        if (name == "forecast") {
            return forecast(arguments);
        }
        if (name == "set_city") {
            return set_city(arguments);
        }
        return text_result("outil inconnu : " + name, true);
    }

    [[nodiscard]] json undo(const json& params) const {
        const auto token = params.is_object() ? params.find("token") : params.end();
        if (!params.is_object() || token == params.end() || !token->is_string()) {
            return text_result("jeton d'annulation absent", true);
        }
        auto done = cities_.undo(token->get<std::string>());
        return done ? text_result(std::move(*done)) : text_result(std::move(done.error()), true);
    }

  private:
    /// Pays de la ville par défaut : préféré quand un nom existe dans
    /// plusieurs pays (« Brest » en France plutôt qu'en Biélorussie).
    [[nodiscard]] std::string preferred_country() const {
        const auto saved = cities_.load();
        return saved ? saved->country_code : std::string{};
    }

    /// Ville demandée, ou ville par défaut si l'argument est absent ou vide.
    Result<Place> resolve(const json& arguments) const {
        const auto city = arguments.find("city");
        if (city != arguments.end() && !city->is_string()) {
            return std::unexpected("le nom de ville doit être un texte");
        }
        if (city == arguments.end() ||
            city->get<std::string>().find_first_not_of(" \t") == std::string::npos) {
            if (auto saved = cities_.load()) {
                return std::move(*saved);
            }
            return std::unexpected("Aucune ville par défaut : précisez une ville, ou réglez-la "
                                   "(par exemple « ma ville est Lyon »).");
        }
        auto valid = validate_city(city->get<std::string>());
        if (!valid) {
            return std::unexpected(std::move(valid.error()));
        }
        return geocode(*valid, preferred_country());
    }

    [[nodiscard]] json current(const json& arguments) const {
        auto place = resolve(arguments);
        if (!place) {
            return text_result(std::move(place.error()), true);
        }
        auto weather = fetch_forecast(*place, 1);
        if (!weather) {
            return text_result(std::move(weather.error()), true);
        }
        if (!weather->current) {
            return text_result("réponse météo incomplète", true);
        }
        return text_result(format_current(*place, *weather->current));
    }

    [[nodiscard]] json forecast(const json& arguments) const {
        int days = kDefaultDays;
        if (const auto value = arguments.find("days"); value != arguments.end()) {
            if (!value->is_number_integer() || value->get<int>() < 1 ||
                value->get<int>() > kMaxDays) {
                return text_result("« days » doit être un entier de 1 à 7", true);
            }
            days = value->get<int>();
        }
        auto place = resolve(arguments);
        if (!place) {
            return text_result(std::move(place.error()), true);
        }
        auto weather = fetch_forecast(*place, days);
        if (!weather) {
            return text_result(std::move(weather.error()), true);
        }
        if (weather->days.empty()) {
            return text_result("réponse météo incomplète", true);
        }
        return text_result(format_forecast(*place, weather->days));
    }

    [[nodiscard]] json set_city(const json& arguments) const {
        const auto city = arguments.find("city");
        if (city == arguments.end() || !city->is_string()) {
            return text_result("« city » est obligatoire", true);
        }
        auto valid = validate_city(city->get<std::string>());
        if (!valid) {
            return text_result(std::move(valid.error()), true);
        }
        auto place = geocode(*valid, preferred_country());
        if (!place) {
            return text_result(std::move(place.error()), true);
        }
        const std::optional<Place> previous = cities_.load();
        auto token = cities_.change(*place);
        if (!token) {
            return text_result(std::move(token.error()), true);
        }
        json result = text_result("Ville par défaut : " + place_label(*place) + ".");
        result["_meta"] = {
            {"avenoir/undo",
             {{"description", previous ? "remettre la ville par défaut à " + place_label(*previous)
                                       : std::string("effacer la ville par défaut")},
              {"token", std::move(*token)}}}};
        return result;
    }

    CityStore cities_;
};

void handle(const Meteo& meteo, const json& message) {
    const auto id = message.find("id");
    if (id == message.end()) {
        return; // Notification (initialized, cancelled) : rien à répondre.
    }
    const auto method = message.value("method", std::string());
    const json params = message.value("params", json::object());
    const auto reply = [&id](json result) {
        send({{"jsonrpc", "2.0"}, {"id", *id}, {"result", std::move(result)}});
    };

    if (method == "initialize") {
        reply({{"protocolVersion", "2025-06-18"},
               {"capabilities", {{"tools", json::object()}}},
               {"serverInfo", {{"name", "avenoir-meteo"}, {"version", kVersion}}}});
    } else if (method == "tools/list") {
        reply({{"tools", tool_list()}});
    } else if (method == "tools/call") {
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
            send({{"jsonrpc", "2.0"},
                  {"id", *id},
                  {"error", {{"code", kInvalidParams}, {"message", "nom d'outil absent"}}}});
            return;
        }
        reply(meteo.call(params["name"].get<std::string>(),
                         params.value("arguments", json::object())));
    } else if (method == "avenoir/undo") {
        reply(meteo.undo(params));
    } else {
        send({{"jsonrpc", "2.0"},
              {"id", *id},
              {"error", {{"code", kMethodNotFound}, {"message", "méthode inconnue"}}}});
    }
}

/// Variable d'environnement, ou rien si elle est absente ou vide.
std::optional<std::string> read_env(const char* name) {
#ifdef _WIN32
    // getenv est déprécié par MSVC : _dupenv_s alloue une copie.
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    std::string value(owned.get());
#else
    const char* raw = std::getenv(name); // NOLINT(concurrency-mt-unsafe) : au démarrage.
    if (raw == nullptr) {
        return std::nullopt;
    }
    std::string value(raw);
#endif
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

/// Dossier de données donné par Avenoir (`AVENOIR_MODULE_DATA`).
std::optional<std::filesystem::path> data_directory() {
    // UTF-8 : sous Windows, le manifeste de l'exécutable en fait la page de codes.
    if (auto data = read_env("AVENOIR_MODULE_DATA")) {
        return std::filesystem::path(std::u8string(data->begin(), data->end()));
    }
    return std::nullopt;
}

} // namespace

int main() {
    try {
        const Meteo meteo(data_directory());
        std::string line;
        while (std::getline(std::cin, line)) {
            const json message = json::parse(line, nullptr, /*allow_exceptions=*/false);
            if (!message.is_object()) {
                std::cerr << "message illisible ignoré\n";
                continue;
            }
            try {
                handle(meteo, message);
            } catch (const std::exception& e) {
                // Une requête mal formée ne fait pas tomber le module.
                std::cerr << "erreur : " << e.what() << '\n';
                if (const auto id = message.find("id"); id != message.end()) {
                    send({{"jsonrpc", "2.0"},
                          {"id", *id},
                          {"error", {{"code", kInvalidParams}, {"message", e.what()}}}});
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "erreur fatale : " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
