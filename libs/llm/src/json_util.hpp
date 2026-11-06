#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace avenoir::llm::detail {

/// Accès sans exception aux champs d'une réponse JSON (dont on ne maîtrise
/// pas la forme) : rien si le champ est absent ou d'un autre type.
inline const nlohmann::json* field(const nlohmann::json& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

inline std::optional<std::string> string_field(const nlohmann::json& object, std::string_view key) {
    const auto* value = field(object, key);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return value->get<std::string>();
}

inline std::optional<std::uint64_t> unsigned_field(const nlohmann::json& object,
                                                   std::string_view key) {
    const auto* value = field(object, key);
    if (value == nullptr || !value->is_number_unsigned()) {
        return std::nullopt;
    }
    return value->get<std::uint64_t>();
}

inline std::optional<double> number_field(const nlohmann::json& object, std::string_view key) {
    const auto* value = field(object, key);
    if (value == nullptr || !value->is_number()) {
        return std::nullopt;
    }
    return value->get<double>();
}

} // namespace avenoir::llm::detail
