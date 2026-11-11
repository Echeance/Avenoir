#include "avenoir/llm/discovery.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "http.hpp"
#include "platform.hpp"

namespace avenoir::llm {

namespace {

constexpr std::size_t kMaxModels = 100;
constexpr std::size_t kMaxModelIdBytes = 200;
/// Délai d'une question à un serveur local : une machine absente du réseau ne
/// répond pas, on ne l'attend pas longtemps.
constexpr std::chrono::milliseconds kProbeTimeout{600};
constexpr std::size_t kScanThreads = 64;

/// Identifiant imprimable : ni caractère de contrôle (séquences
/// d'échappement du terminal), ni espace en bord.
bool is_clean_model_id(std::string_view id) {
    return !id.empty() && id.size() <= kMaxModelIdBytes && id.front() != ' ' && id.back() != ' ' &&
           std::ranges::none_of(id, [](char c) {
               const auto byte = static_cast<unsigned char>(c);
               return byte < 0x20U || byte == 0x7FU;
           });
}

/// Les quatre octets d'une IPv4 écrite « a.b.c.d », ou rien.
std::optional<std::array<std::uint8_t, 4>> parse_ipv4(std::string_view text) {
    std::array<std::uint8_t, 4> bytes{};
    std::size_t index = 0;
    const char* cursor = text.data();
    const char* end =
        text.data() + text.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (; index < bytes.size(); ++index) {
        unsigned value = 0;
        const auto [next, error] = std::from_chars(cursor, end, value);
        if (error != std::errc{} || next == cursor || value > 255 || next - cursor > 3) {
            return std::nullopt;
        }
        bytes.at(index) = static_cast<std::uint8_t>(value);
        cursor = next;
        if (index + 1 < bytes.size()) {
            if (cursor == end || *cursor != '.') {
                return std::nullopt;
            }
            ++cursor; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        }
    }
    if (cursor != end) {
        return std::nullopt;
    }
    return bytes;
}

ServerProbe default_probe() {
    return [](const std::string& url) { return list_server_models(url, kProbeTimeout); };
}

} // namespace

std::optional<std::vector<std::string>> parse_model_list(const nlohmann::json& response) {
    if (!response.is_object() || !response.contains("data") || !response["data"].is_array()) {
        return std::nullopt;
    }
    std::vector<std::string> models;
    for (const auto& entry : response["data"]) {
        if (models.size() == kMaxModels) {
            break;
        }
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string()) {
            continue;
        }
        auto id = entry["id"].get<std::string>();
        if (is_clean_model_id(id) && !std::ranges::contains(models, id)) {
            models.push_back(std::move(id));
        }
    }
    return models;
}

core::Result<std::vector<std::string>> list_server_models(const std::string& base_url,
                                                          std::chrono::milliseconds timeout) {
    auto response = detail::get_json(base_url + "/models", timeout);
    if (!response) {
        return std::unexpected(std::move(response.error()));
    }
    auto models = parse_model_list(*response);
    if (!models) {
        return core::make_error(core::ErrorCode::Llm,
                                base_url + " : liste des modèles illisible (API OpenAI attendue)");
    }
    return std::move(*models);
}

bool is_private_ipv4(std::string_view address) {
    const auto bytes = parse_ipv4(address);
    if (!bytes) {
        return false;
    }
    const auto [a, b, c, d] = *bytes;
    (void)c;
    (void)d;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

std::vector<std::string> subnet_neighbours(std::string_view address) {
    std::vector<std::string> hosts;
    const auto parsed = parse_ipv4(address);
    if (!parsed || !is_private_ipv4(address)) {
        return hosts;
    }
    const auto& bytes = *parsed;
    hosts.reserve(253);
    for (unsigned host = 1; host <= 254; ++host) {
        if (host != bytes[3]) {
            hosts.push_back(std::format("{}.{}.{}.{}", bytes[0], bytes[1], bytes[2], host));
        }
    }
    return hosts;
}

std::optional<std::string> local_network_address() {
    auto address = platform::default_route_ipv4();
    if (!address || !is_private_ipv4(*address)) {
        return std::nullopt;
    }
    return address;
}

std::vector<DiscoveredServer> discover_local_servers(const ServerProbe& probe) {
    return discover_servers({"127.0.0.1"}, std::stop_token{}, probe);
}

std::vector<DiscoveredServer> discover_servers(const std::vector<std::string>& hosts,
                                               const std::stop_token& stop,
                                               const ServerProbe& probe) {
    const ServerProbe ask = probe ? probe : default_probe();
    std::vector<std::string> urls;
    urls.reserve(hosts.size() * kDefaultServerPorts.size());
    for (const auto& host : hosts) {
        for (const auto port : kDefaultServerPorts) {
            urls.push_back(std::format("http://{}:{}/v1", host, port));
        }
    }
    // Un résultat par adresse, rempli par le fil qui l'a sondée.
    std::vector<std::optional<std::vector<std::string>>> found(urls.size());
    std::atomic<std::size_t> next{0};
    {
        std::vector<std::jthread> workers;
        const std::size_t count = std::min(kScanThreads, urls.size());
        workers.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            workers.emplace_back([&] {
                for (std::size_t index = next++; index < urls.size(); index = next++) {
                    if (stop.stop_requested()) {
                        return;
                    }
                    if (auto models = ask(urls[index])) {
                        found[index] = std::move(*models);
                    }
                }
            });
        }
    } // attend la fin de tous les fils
    std::vector<DiscoveredServer> servers;
    for (std::size_t i = 0; i < urls.size(); ++i) {
        if (auto& models = found[i]) {
            servers.push_back(DiscoveredServer{.base_url = urls[i], .models = std::move(*models)});
        }
    }
    return servers;
}

} // namespace avenoir::llm
