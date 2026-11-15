#include "updates.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace avenoir::daemon {

namespace {

using nlohmann::json;

struct Version {
    std::array<unsigned, 3> numbers{};
    int stage = 3; ///< alpha 0, beta 1, rc 2, stable 3.
    unsigned stage_number = 0;
};

std::optional<unsigned> number(std::string_view text) {
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = (value * 10U) + static_cast<unsigned>(c - '0');
    }
    return value;
}

int stage_rank(std::string_view stage) {
    if (stage == "alpha") {
        return 0;
    }
    if (stage == "beta") {
        return 1;
    }
    if (stage == "rc") {
        return 2;
    }
    return -1;
}

std::optional<Version> parse(std::string_view text) {
    if (text.starts_with('v')) {
        text.remove_prefix(1);
    }
    Version version;
    const auto dash = text.find('-');
    std::string_view core = text.substr(0, dash);
    for (std::size_t at = 0; at < 3; ++at) {
        const auto dot = core.find('.');
        if ((at < 2) == (dot == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto value = number(core.substr(0, dot));
        if (!value) {
            return std::nullopt;
        }
        version.numbers.at(at) = *value;
        core = dot == std::string_view::npos ? std::string_view{} : core.substr(dot + 1);
    }
    if (dash != std::string_view::npos) {
        const std::string_view suffix = text.substr(dash + 1);
        const auto dot = suffix.find('.');
        const std::string_view stage = suffix.substr(0, dot);
        version.stage = stage_rank(stage);
        const auto value = dot == std::string_view::npos ? std::optional<unsigned>{0}
                                                         : number(suffix.substr(dot + 1));
        if (version.stage < 0 || !value) {
            return std::nullopt;
        }
        version.stage_number = *value;
    }
    return version;
}

} // namespace

bool is_newer(std::string_view candidate, std::string_view current) {
    const auto a = parse(candidate);
    const auto b = parse(current);
    if (!a || !b) {
        return false;
    }
    return std::tie(a->numbers, a->stage, a->stage_number) >
           std::tie(b->numbers, b->stage, b->stage_number);
}

std::optional<Release> latest_release(const json& releases, bool prereleases) {
    if (!releases.is_array()) {
        return std::nullopt;
    }
    std::optional<Release> best;
    for (const auto& release : releases) {
        if (!release.is_object() || release.value("draft", false) ||
            !release.contains("tag_name") || !release["tag_name"].is_string()) {
            continue;
        }
        const bool pre = release.value("prerelease", false);
        std::string tag = release["tag_name"].get<std::string>();
        if ((pre && !prereleases) || !parse(tag)) {
            continue;
        }
        if (tag.starts_with('v')) {
            tag.erase(0, 1);
        }
        std::string url = release.value("html_url", std::string{});
        if (!url.starts_with("https://github.com/")) {
            url.clear(); // seule une page GitHub est montrée
        }
        if (!best || is_newer(tag, best->version)) {
            best = Release{.version = std::move(tag), .url = std::move(url), .prerelease = pre};
        }
    }
    return best;
}

core::Result<json> fetch_releases(const std::stop_token& stop) {
    constexpr std::size_t kMaxBody = std::size_t{2} * 1024 * 1024;
    const cpr::Response response =
        cpr::Get(cpr::Url{std::string(kReleasesUrl)},
                 cpr::Header{{"Accept", "application/vnd.github+json"}, {"User-Agent", "Avenoir"}},
                 cpr::Timeout{std::chrono::seconds{5}},
                 cpr::ProgressCallback{[&stop](auto /*download_total*/, auto download_now,
                                               auto /*upload_total*/, auto /*upload_now*/,
                                               auto /*userdata*/) {
                     return !stop.stop_requested() && std::cmp_less_equal(download_now, kMaxBody);
                 }});
    if (stop.stop_requested()) {
        return core::make_error(core::ErrorCode::Cancelled, "vérification interrompue");
    }
    if (response.error.code != cpr::ErrorCode::OK || response.status_code != 200) {
        return core::make_error(core::ErrorCode::Config,
                                response.error.code != cpr::ErrorCode::OK
                                    ? "GitHub injoignable : " + response.error.message
                                    : "GitHub a répondu " + std::to_string(response.status_code));
    }
    json parsed = json::parse(response.text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        return core::make_error(core::ErrorCode::Config, "réponse de GitHub illisible");
    }
    return parsed;
}

} // namespace avenoir::daemon
