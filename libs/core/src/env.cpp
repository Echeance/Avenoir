#include "avenoir/core/env.hpp"

#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <cstdlib>
#include <memory>
#else
#include <cstdlib>
#endif

namespace avenoir::core {

std::optional<std::string> read_env(std::string_view name) {
    const std::string key(name);
#ifdef _WIN32
    // getenv est déprécié par MSVC : _dupenv_s alloue une copie, libérée par free.
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, key.c_str()) != 0 || raw == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    std::string value(owned.get());
#else
    // Lecture seule au démarrage, avant tout thread (voir l'en-tête).
    const char* raw = std::getenv(key.c_str()); // NOLINT(concurrency-mt-unsafe)
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

} // namespace avenoir::core
