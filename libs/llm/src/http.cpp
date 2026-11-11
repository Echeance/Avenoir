#include "http.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

namespace avenoir::llm::detail {

std::string excerpt(std::string_view text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return std::string(text);
    }
    // Recule jusqu'au début d'un caractère UTF-8 pour ne pas le couper.
    std::size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return std::string(text.substr(0, end)) + "…";
}

core::Result<nlohmann::json> post_json(const HttpRequest& request, const std::stop_token& stop) {
    cpr::Header header{{"Content-Type", "application/json"}};
    for (const auto& [key, value] : request.headers) {
        header[key] = value;
    }

    // Le rappel de progression permet d'interrompre une requête en cours.
    const cpr::Response response = cpr::Post(
        cpr::Url{request.url}, header, cpr::Body{request.body}, cpr::Timeout{request.timeout},
        cpr::ProgressCallback{[&stop](auto /*download_total*/, auto /*download_now*/,
                                      auto /*upload_total*/, auto /*upload_now*/,
                                      auto /*userdata*/) { return !stop.stop_requested(); }});

    if (stop.stop_requested()) {
        return core::make_error(core::ErrorCode::Cancelled, {});
    }
    if (response.error.code != cpr::ErrorCode::OK) {
        return core::make_error(core::ErrorCode::Llm,
                                "connexion à " + request.url +
                                    " impossible : " + response.error.message);
    }

    nlohmann::json body = nlohmann::json::parse(response.text, nullptr, /*allow_exceptions=*/false);
    if (response.status_code < 200 || response.status_code >= 300) {
        // OpenAI et Anthropic renvoient { "error": { "message": "…" } }.
        std::string message = excerpt(response.text);
        if (body.is_object() && body.contains("error")) {
            const auto& error = body["error"];
            if (error.is_object() && error.contains("message") && error["message"].is_string()) {
                message = error["message"].get<std::string>();
            } else if (error.is_string()) {
                message = error.get<std::string>();
            }
        }
        return core::make_error(core::ErrorCode::Llm,
                                "HTTP " + std::to_string(response.status_code) + " : " + message);
    }
    if (body.is_discarded()) {
        return core::make_error(core::ErrorCode::Llm,
                                "réponse illisible (JSON attendu) : " + excerpt(response.text));
    }
    return body;
}

core::Result<void>
post_stream(const HttpRequest& request,
            const std::function<core::Result<void>(std::string_view data)>& on_data,
            const std::stop_token& stop) {
    cpr::Header header{{"Content-Type", "application/json"}, {"Accept", "text/event-stream"}};
    for (const auto& [key, value] : request.headers) {
        header[key] = value;
    }
    constexpr std::size_t kMaxKept = std::size_t{64} * 1024; // corps gardé pour un message d'erreur
    std::string pending;                                     // ligne en cours de réception
    std::string kept;
    std::optional<core::Error> failure;
    const auto on_chunk = [&](std::string_view chunk, intptr_t /*userdata*/) {
        if (stop.stop_requested()) {
            return false;
        }
        if (kept.size() < kMaxKept) {
            kept.append(chunk.substr(0, kMaxKept - kept.size()));
        }
        pending.append(chunk);
        std::size_t start = 0;
        for (auto end = pending.find('\n'); end != std::string::npos;
             end = pending.find('\n', start)) {
            std::string_view line = std::string_view(pending).substr(start, end - start);
            start = end + 1;
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            if (!line.starts_with("data:")) {
                continue; // « event: … », commentaires, lignes vides
            }
            line.remove_prefix(5);
            if (line.starts_with(' ')) {
                line.remove_prefix(1);
            }
            if (auto handled = on_data(line); !handled) {
                failure = std::move(handled.error());
                return false;
            }
        }
        pending.erase(0, start);
        return true;
    };
    const cpr::Response response =
        cpr::Post(cpr::Url{request.url}, header, cpr::Body{request.body},
                  cpr::Timeout{request.timeout}, cpr::WriteCallback{on_chunk});

    if (stop.stop_requested()) {
        return core::make_error(core::ErrorCode::Cancelled, {});
    }
    if (failure) {
        return std::unexpected(std::move(*failure));
    }
    if (response.status_code != 0 && (response.status_code < 200 || response.status_code >= 300)) {
        const nlohmann::json body =
            nlohmann::json::parse(kept, nullptr, /*allow_exceptions=*/false);
        std::string message = excerpt(kept);
        if (body.is_object() && body.contains("error")) {
            const auto& error = body["error"];
            if (error.is_object() && error.contains("message") && error["message"].is_string()) {
                message = error["message"].get<std::string>();
            } else if (error.is_string()) {
                message = error.get<std::string>();
            }
        }
        return core::make_error(core::ErrorCode::Llm,
                                "HTTP " + std::to_string(response.status_code) + " : " + message);
    }
    if (response.error.code != cpr::ErrorCode::OK) {
        return core::make_error(core::ErrorCode::Llm,
                                "connexion à " + request.url +
                                    " impossible : " + response.error.message);
    }
    return {};
}

core::Result<nlohmann::json> get_json(const std::string& url, std::chrono::milliseconds timeout) {
    const cpr::Response response = cpr::Get(cpr::Url{url}, cpr::Timeout{timeout});
    if (response.error.code != cpr::ErrorCode::OK) {
        return core::make_error(core::ErrorCode::Llm,
                                "connexion à " + url + " impossible : " + response.error.message);
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        return core::make_error(core::ErrorCode::Llm, "HTTP " +
                                                          std::to_string(response.status_code) +
                                                          " : " + excerpt(response.text));
    }
    nlohmann::json body = nlohmann::json::parse(response.text, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded()) {
        return core::make_error(core::ErrorCode::Llm,
                                "réponse illisible (JSON attendu) : " + excerpt(response.text));
    }
    return body;
}

} // namespace avenoir::llm::detail
