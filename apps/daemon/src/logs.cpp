#include "logs.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

namespace avenoir::daemon {

/// Fin d'un fichier texte (log) : `max` octets au plus, à partir d'un début
/// de ligne, octets UTF-8 invalides retirés (le texte part en JSON).
std::string read_log_tail(const std::filesystem::path& path, std::size_t max) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const auto size = static_cast<std::size_t>(std::max<std::streamoff>(file.tellg(), 0));
    const std::size_t start = size > max ? size - max : 0;
    file.seekg(static_cast<std::streamoff>(start));
    std::string raw(size - start, '\0');
    file.read(raw.data(), static_cast<std::streamsize>(raw.size()));
    raw.resize(static_cast<std::size_t>(std::max<std::streamsize>(file.gcount(), 0)));
    if (start > 0) {
        const auto line = raw.find('\n');
        raw.erase(0, line == std::string::npos ? raw.size() : line + 1);
    }
    std::string out;
    out.reserve(raw.size());
    std::size_t at = 0;
    while (at < raw.size()) {
        const auto lead = static_cast<unsigned char>(raw[at]);
        std::size_t length = 1;
        if (lead >= 0xF0U && lead < 0xF5U) {
            length = 4;
        } else if (lead >= 0xE0U) {
            length = 3;
        } else if (lead >= 0xC2U) {
            length = 2;
        } else if (lead >= 0x80U) {
            ++at; // octet de suite isolé ou invalide
            continue;
        }
        if (length == 1 || lead >= 0xF5U) {
            if (lead < 0x80U) {
                out += raw[at];
            }
            ++at;
            continue;
        }
        bool valid = at + length <= raw.size();
        for (std::size_t next = 1; valid && next < length; ++next) {
            valid = (static_cast<unsigned char>(raw[at + next]) & 0xC0U) == 0x80U;
        }
        if (valid) {
            out.append(raw, at, length);
            at += length;
        } else {
            ++at;
        }
    }
    return out;
}

} // namespace avenoir::daemon
