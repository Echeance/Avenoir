#include "http.hpp"

#include <cstddef>
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

} // namespace avenoir::llm::detail
