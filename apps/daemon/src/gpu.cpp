#include "gpu.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace avenoir::daemon {

namespace {

/// Valeur de la clé `key` (« clé:\tvaleur ») dans `text`, ou rien.
std::optional<std::string_view> field(std::string_view text, std::string_view key) {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        std::string_view line =
            text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at = end == std::string_view::npos ? text.size() : end + 1;
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == ':') {
            line.remove_prefix(key.size() + 1);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.remove_prefix(1);
            }
            return line;
        }
    }
    return std::nullopt;
}

/// « 216936 KiB » → octets.
std::optional<std::uint64_t> memory(std::string_view value) {
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(value.begin(), value.end(), number);
    if (error != std::errc{}) {
        return std::nullopt;
    }
    std::string_view unit(end, value.end());
    while (!unit.empty() && unit.front() == ' ') {
        unit.remove_prefix(1);
    }
    if (unit == "KiB") {
        return number * 1024;
    }
    if (unit == "MiB") {
        return number * 1024 * 1024;
    }
    if (unit == "GiB") {
        return number * 1024 * 1024 * 1024;
    }
    return number; // octets
}

} // namespace

std::optional<DrmUsage> parse_drm_fdinfo(std::string_view text) {
    const auto client = field(text, "drm-client-id");
    if (!client) {
        return std::nullopt;
    }
    auto vram = field(text, "drm-resident-vram");
    if (!vram) {
        vram = field(text, "drm-memory-vram");
    }
    DrmUsage usage{.client = std::string(*client), .video_memory = 0};
    if (vram) {
        usage.video_memory = memory(*vram).value_or(0);
    }
    return usage;
}

bool is_model_server(std::string_view program) {
    constexpr std::array<std::string_view, 7> kServers{
        "ollama", "llama-server", "llama.cpp", "lms", "LM Studio", "avenoird", "avenoir"};
    return std::ranges::any_of(
        kServers, [program](std::string_view server) { return program.starts_with(server); });
}

} // namespace avenoir::daemon
