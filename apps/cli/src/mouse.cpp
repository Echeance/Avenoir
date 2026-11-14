#include "mouse.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "avenoir/terminal/style.hpp"

namespace avenoir::cli {

std::optional<MouseEvent> left_button_event(unsigned int button, std::size_t column,
                                            std::size_t row, bool release) {
    const unsigned int plain = button & ~0x1CU; // sans Maj, Méta, Ctrl
    if (column == 0 || row == 0) {
        return std::nullopt;
    }
    if (release && (plain == 0 || plain == 3)) {
        return MouseEvent{.kind = MouseEvent::Kind::Release, .column = column, .row = row};
    }
    if (release) {
        return std::nullopt;
    }
    if (plain == 0) {
        return MouseEvent{.kind = MouseEvent::Kind::Press, .column = column, .row = row};
    }
    if (plain == 32) {
        return MouseEvent{.kind = MouseEvent::Kind::Drag, .column = column, .row = row};
    }
    return std::nullopt;
}

std::optional<MouseEvent> parse_sgr_mouse(std::string_view sequence) {
    if (!sequence.starts_with("\x1b[<") || sequence.size() < 7) {
        return std::nullopt;
    }
    const char last = sequence.back();
    if (last != 'M' && last != 'm') {
        return std::nullopt;
    }
    std::array<std::size_t, 3> fields{};
    std::size_t field = 0;
    bool digits = false;
    for (const char c : sequence.substr(3, sequence.size() - 4)) {
        if (c == ';') {
            if (!digits || field == 2) {
                return std::nullopt;
            }
            ++field;
            digits = false;
        } else if (c >= '0' && c <= '9' && fields.at(field) < 100000) {
            fields.at(field) = (fields.at(field) * 10) + static_cast<std::size_t>(c - '0');
            digits = true;
        } else {
            return std::nullopt;
        }
    }
    if (field != 2 || !digits) {
        return std::nullopt;
    }
    return left_button_event(static_cast<unsigned int>(fields[0]), fields[1], fields[2],
                             last == 'm');
}

std::string columns_slice(std::string_view plain, std::size_t from, std::size_t to) {
    std::string out;
    std::size_t column = 0;
    std::size_t at = 0;
    while (at < plain.size() && column < to) {
        std::size_t end = at + 1;
        while (end < plain.size() && (static_cast<unsigned char>(plain[end]) & 0xC0U) == 0x80U) {
            ++end;
        }
        const std::string_view character = plain.substr(at, end - at);
        const std::size_t width = terminal::display_width(character);
        if (column + width > from) {
            out += character;
        }
        column += width;
        at = end;
    }
    return out;
}

std::string base64(std::string_view bytes) {
    constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    std::size_t at = 0;
    while (at + 2 < bytes.size()) {
        const auto value =
            (static_cast<unsigned int>(static_cast<unsigned char>(bytes[at])) << 16U) |
            (static_cast<unsigned int>(static_cast<unsigned char>(bytes[at + 1])) << 8U) |
            static_cast<unsigned int>(static_cast<unsigned char>(bytes[at + 2]));
        out += kAlphabet[(value >> 18U) & 0x3FU];
        out += kAlphabet[(value >> 12U) & 0x3FU];
        out += kAlphabet[(value >> 6U) & 0x3FU];
        out += kAlphabet[value & 0x3FU];
        at += 3;
    }
    const std::size_t rest = bytes.size() - at;
    if (rest > 0) {
        unsigned int value = static_cast<unsigned int>(static_cast<unsigned char>(bytes[at]))
                             << 16U;
        if (rest == 2) {
            value |= static_cast<unsigned int>(static_cast<unsigned char>(bytes[at + 1])) << 8U;
        }
        out += kAlphabet[(value >> 18U) & 0x3FU];
        out += kAlphabet[(value >> 12U) & 0x3FU];
        out += rest == 2 ? kAlphabet[(value >> 6U) & 0x3FU] : '=';
        out += '=';
    }
    return out;
}

} // namespace avenoir::cli
