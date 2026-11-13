// Contrôle du système (ROADMAP 2.1) : son, multimédia, luminosité, mode
// sombre, verrouillage, notifications, alimentation. Le code propre à chaque
// OS est dans src/platform/ ; ici, la validation, les phrases et l'annulation.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/tools/tools.hpp"

#include "platform.hpp"
#include "tools_impl.hpp"

namespace avenoir::tools {

namespace {

core::ToolError execution_error(std::string message) {
    return core::ToolError{.kind = core::ToolErrorKind::Execution, .message = std::move(message)};
}

core::ToolError invalid_arguments(std::string message) {
    return core::ToolError{.kind = core::ToolErrorKind::InvalidArguments,
                           .message = std::move(message)};
}

} // namespace

namespace detail {

/// Entier facultatif : nombre JSON ou texte (« 30 », « 30 % ») venu de la
/// voie rapide. Erreur s'il est présent mais illisible ou hors limites.
std::expected<std::optional<int>, core::ToolError> optional_int(const nlohmann::json& arguments,
                                                                const char* key, int min, int max) {
    if (!arguments.is_object() || !arguments.contains(key) || arguments[key].is_null()) {
        return std::optional<int>{};
    }
    const auto& value = arguments[key];
    long long number = 0;
    if (value.is_number_integer()) {
        number = value.get<long long>();
    } else if (value.is_number()) {
        const double real = value.get<double>();
        if (!std::isfinite(real) || real < -1e6 || real > 1e6) {
            return std::unexpected(invalid_arguments(std::string(key) + " : nombre attendu"));
        }
        number = std::llround(real);
    } else if (value.is_string()) {
        std::string text = value.get<std::string>();
        std::erase_if(text, [](char c) { return c == ' ' || c == '%'; });
        bool negative = false;
        if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
            negative = text.front() == '-';
            text.erase(text.begin());
        }
        if (text.empty() || text.size() > 4 ||
            !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
            return std::unexpected(invalid_arguments(std::string(key) + " : nombre attendu"));
        }
        number = std::stoll(text) * (negative ? -1 : 1);
    } else {
        return std::unexpected(invalid_arguments(std::string(key) + " : nombre attendu"));
    }
    if (number < min || number > max) {
        return std::unexpected(
            invalid_arguments(std::format("{} : entre {} et {} attendu", key, min, max)));
    }
    return std::optional<int>(static_cast<int>(number));
}

/// Texte de notification : sans caractère de contrôle (C0, DEL, C1) ni marque
/// bidirectionnelle ou invisible (qui maquillerait le texte affiché), borné.
std::string clean_notification(std::string_view text, std::size_t max_bytes) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x20U || byte == 0x7FU) {
            out += ' ';
            continue;
        }
        if (byte == 0xC2U && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x80U && next <= 0x9FU) { // C1
                out += ' ';
                ++i;
                continue;
            }
        }
        if ((byte == 0xE2U || byte == 0xEFU) && i + 2 < text.size()) {
            const auto b1 = static_cast<unsigned char>(text[i + 1]);
            const auto b2 = static_cast<unsigned char>(text[i + 2]);
            const bool invisible =
                (byte == 0xE2U && b1 == 0x80U &&
                 ((b2 >= 0x8BU && b2 <= 0x8FU) ||                               // U+200B–200F
                  (b2 >= 0xAAU && b2 <= 0xAEU))) ||                             // U+202A–202E
                (byte == 0xE2U && b1 == 0x81U && b2 >= 0xA6U && b2 <= 0xA9U) || // U+2066–2069
                (byte == 0xEFU && b1 == 0xBBU && b2 == 0xBFU);                  // U+FEFF
            if (invisible) {
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    if (out.size() > max_bytes) {
        std::size_t end = max_bytes;
        while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xC0U) == 0x80U) {
            --end; // ne pas couper un caractère UTF-8
        }
        out.resize(end);
        out += "…";
    }
    return out;
}

} // namespace detail

namespace {

using detail::clean_notification;
using detail::optional_int;

std::optional<bool> optional_bool(const nlohmann::json& arguments, const char* key) {
    if (arguments.is_object() && arguments.contains(key) && arguments[key].is_boolean()) {
        return arguments[key].get<bool>();
    }
    return std::nullopt;
}

std::string volume_sentence(const platform::VolumeState& state) {
    return state.muted ? std::format("Son coupé (volume à {} %).", state.level)
                       : std::format("Volume : {} %.", state.level);
}

// ─── Son ────────────────────────────────────────────────────────────────────

class VolumeTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "set_volume",
            .description =
                "Règle le son de la sortie audio : `level` (0 à 100 %), ou `change` (en points, "
                "ex. +10 pour monter le son, -10 pour le baisser), et/ou `muted` (couper ou "
                "remettre le son). Sans argument, donne le volume actuel.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"level", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                               {"change",
                                {{"type", "integer"}, {"minimum", -100}, {"maximum", 100}}},
                               {"muted", {{"type", "boolean"}}}}}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto level = optional_int(arguments, "level", 0, 100);
        auto change = optional_int(arguments, "change", -100, 100);
        if (!level) {
            return std::unexpected(level.error());
        }
        if (!change) {
            return std::unexpected(change.error());
        }
        const auto muted = optional_bool(arguments, "muted");
        auto before = platform::get_volume();
        if (!before) {
            return std::unexpected(execution_error(before.error()));
        }
        if (!*level && !*change && !muted) {
            return core::ToolOutput{.content = volume_sentence(*before)};
        }
        platform::VolumeState target = *before;
        if (*level) {
            target.level = **level;
        } else if (*change) {
            target.level = std::clamp(before->level + **change, 0, 100);
        }
        if (muted) {
            target.muted = *muted;
        } else if (*level || *change) {
            target.muted = false; // régler le volume remet le son
        }
        if (target.level != before->level) {
            if (auto done = platform::set_volume(target.level); !done) {
                return std::unexpected(execution_error(done.error()));
            }
        }
        if (target.muted != before->muted) {
            if (auto done = platform::set_muted(target.muted); !done) {
                return std::unexpected(execution_error(done.error()));
            }
        }
        const platform::VolumeState previous = *before;
        return core::ToolOutput{
            .content = volume_sentence(target),
            .undo =
                core::UndoAction{
                    .description =
                        std::format("remettre le son comme avant ({})", volume_sentence(previous)),
                    .run = [previous, target] -> core::ToolResult {
                        auto now = platform::get_volume();
                        if (!now || now->level != target.level || now->muted != target.muted) {
                            return std::unexpected(
                                execution_error("le son a changé depuis : rien n'est touché"));
                        }
                        if (auto done = platform::set_volume(previous.level); !done) {
                            return std::unexpected(execution_error(done.error()));
                        }
                        if (auto done = platform::set_muted(previous.muted); !done) {
                            return std::unexpected(execution_error(done.error()));
                        }
                        return core::ToolOutput{.content = volume_sentence(previous)};
                    },
                },
        };
    }
};

// ─── Multimédia ─────────────────────────────────────────────────────────────

class MediaTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "media_control",
            .description = "Commande le lecteur multimédia actif (musique, vidéo) : lecture ou "
                           "pause, morceau suivant ou précédent, arrêt ; `now_playing` dit ce qui "
                           "passe.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"action",
                                {{"type", "string"},
                                 {"enum",
                                  {"play_pause", "play", "pause", "next", "previous", "stop",
                                   "now_playing"}}}}}},
                             {"required", nlohmann::json::array({"action"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        const std::string action =
            arguments.is_object() && arguments.contains("action") && arguments["action"].is_string()
                ? arguments["action"].get<std::string>()
                : "";
        if (action == "now_playing") {
            auto playing = platform::now_playing();
            if (!playing) {
                return std::unexpected(execution_error(playing.error()));
            }
            return core::ToolOutput{.content = "En cours : " + *playing + "."};
        }
        static const std::vector<
            std::tuple<std::string_view, platform::MediaCommand, std::string_view>>
            commands{
                {"play_pause", platform::MediaCommand::PlayPause, "Lecture ou pause"},
                {"play", platform::MediaCommand::Play, "Lecture"},
                {"pause", platform::MediaCommand::Pause, "Pause"},
                {"next", platform::MediaCommand::Next, "Morceau suivant"},
                {"previous", platform::MediaCommand::Previous, "Morceau précédent"},
                {"stop", platform::MediaCommand::Stop, "Arrêt"},
        };
        const auto found = std::ranges::find_if(
            commands, [&action](const auto& entry) { return std::get<0>(entry) == action; });
        if (found == commands.end()) {
            return std::unexpected(invalid_arguments("action inconnue : « " + action + " »"));
        }
        if (auto done = platform::media_command(std::get<1>(*found)); !done) {
            return std::unexpected(execution_error(done.error()));
        }
        std::string content = std::string(std::get<2>(*found)) + ".";
        if (auto playing = platform::now_playing()) {
            content += " En cours : " + *playing + ".";
        }
        return core::ToolOutput{.content = std::move(content)};
    }
};

// ─── Luminosité ─────────────────────────────────────────────────────────────

class BrightnessTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "set_brightness",
            .description = "Règle la luminosité de l'écran intégré : `level` (1 à 100 %) ou "
                           "`change` (en points, ex. +10 / -10). Sans argument, donne la "
                           "luminosité actuelle.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"level", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                               {"change",
                                {{"type", "integer"}, {"minimum", -100}, {"maximum", 100}}}}}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto level = optional_int(arguments, "level", 1, 100);
        auto change = optional_int(arguments, "change", -100, 100);
        if (!level) {
            return std::unexpected(level.error());
        }
        if (!change) {
            return std::unexpected(change.error());
        }
        const std::optional<int> wanted_level = *level;
        const std::optional<int> wanted_change = *change;
        auto before = platform::get_brightness();
        if (!before) {
            return std::unexpected(execution_error(before.error()));
        }
        // Jamais 0 % : un écran noir ne se règle plus à l'œil.
        int target = 0;
        if (wanted_level) {
            target = *wanted_level;
        } else if (wanted_change) {
            target = std::clamp(*before + *wanted_change, 1, 100);
        } else {
            return core::ToolOutput{.content = std::format("Luminosité : {} %.", *before)};
        }
        if (auto done = platform::set_brightness(target); !done) {
            return std::unexpected(execution_error(done.error()));
        }
        const int previous = *before;
        return core::ToolOutput{
            .content = std::format("Luminosité : {} %.", target),
            .undo =
                core::UndoAction{
                    .description = std::format("remettre la luminosité à {} %", previous),
                    .run = [previous, target] -> core::ToolResult {
                        auto now = platform::get_brightness();
                        if (!now || std::abs(*now - target) > 1) {
                            return std::unexpected(execution_error(
                                "la luminosité a changé depuis : rien n'est touché"));
                        }
                        if (auto done = platform::set_brightness(previous); !done) {
                            return std::unexpected(execution_error(done.error()));
                        }
                        return core::ToolOutput{.content =
                                                    std::format("Luminosité : {} %.", previous)};
                    },
                },
        };
    }
};

// ─── Mode sombre ────────────────────────────────────────────────────────────

class DarkModeTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "set_dark_mode",
            .description = "Active (`dark` = true) ou désactive (false : mode clair) le mode "
                           "sombre du système. Sans argument, dit lequel est actif.",
            .input_schema = {{"type", "object"}, {"properties", {{"dark", {{"type", "boolean"}}}}}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto before = platform::get_dark_mode();
        if (!before) {
            return std::unexpected(execution_error(before.error()));
        }
        const auto sentence = [](bool dark) {
            return dark ? std::string("Mode sombre activé.") : std::string("Mode clair activé.");
        };
        const auto dark = optional_bool(arguments, "dark");
        if (!dark) {
            return core::ToolOutput{.content = *before ? "Le mode sombre est actif."
                                                       : "Le mode clair est actif."};
        }
        if (*dark == *before) {
            return core::ToolOutput{.content = *dark ? "Le mode sombre est déjà actif."
                                                     : "Le mode clair est déjà actif."};
        }
        if (auto done = platform::set_dark_mode(*dark); !done) {
            return std::unexpected(execution_error(done.error()));
        }
        const bool target = *dark;
        return core::ToolOutput{
            .content = sentence(target),
            .undo =
                core::UndoAction{
                    .description = target ? "revenir au mode clair" : "revenir au mode sombre",
                    .run = [target, sentence] -> core::ToolResult {
                        auto now = platform::get_dark_mode();
                        if (!now || *now != target) {
                            return std::unexpected(
                                execution_error("le mode a changé depuis : rien n'est touché"));
                        }
                        if (auto done = platform::set_dark_mode(!target); !done) {
                            return std::unexpected(execution_error(done.error()));
                        }
                        return core::ToolOutput{.content = sentence(!target)};
                    },
                },
        };
    }
};

// ─── Verrouillage ───────────────────────────────────────────────────────────

class LockTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "lock_screen",
            .description = "Verrouille la session (écran de verrouillage ; le mot de passe "
                           "est demandé pour revenir).",
            .input_schema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& /*arguments*/,
                                        const std::stop_token& /*stop*/) const override {
        if (auto done = platform::lock_screen(); !done) {
            return std::unexpected(execution_error(done.error()));
        }
        return core::ToolOutput{.content = "Session verrouillée."};
    }
};

// ─── Notification ───────────────────────────────────────────────────────────

class NotifyTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "notify",
            .description = "Affiche une notification du système (titre court et message), par "
                           "exemple pour un rappel.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"title", {{"type", "string"}}}, {"message", {{"type", "string"}}}}},
                             {"required", nlohmann::json::array({"message"})}},
            .risk = core::RiskLevel::ReadOnly,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        const auto text = [&arguments](const char* key) {
            return arguments.is_object() && arguments.contains(key) && arguments[key].is_string()
                       ? arguments[key].get<std::string>()
                       : std::string{};
        };
        const std::string message = clean_notification(text("message"), 500);
        if (message.find_first_not_of(' ') == std::string::npos) {
            return std::unexpected(invalid_arguments("message vide"));
        }
        std::string title = clean_notification(text("title"), 80);
        if (title.find_first_not_of(' ') == std::string::npos) {
            title = "Avenoir";
        }
        if (auto done = platform::notify(title, message); !done) {
            return std::unexpected(execution_error(done.error()));
        }
        return core::ToolOutput{.content = "Notification affichée."};
    }
};

// ─── Alimentation ───────────────────────────────────────────────────────────

class PowerTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "power",
            .description = "Met l'ordinateur en veille (`suspend`), l'éteint (`shutdown`) ou le "
                           "redémarre (`reboot`). Avenoir demande lui-même confirmation à "
                           "l'utilisateur avant d'agir : appelle l'outil directement, sans "
                           "demander d'abord.",
            .input_schema =
                {{"type", "object"},
                 {"properties",
                  {{"action", {{"type", "string"}, {"enum", {"suspend", "shutdown", "reboot"}}}}}},
                 {"required", nlohmann::json::array({"action"})}},
            .risk = core::RiskLevel::Sensitive,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        const std::string action =
            arguments.is_object() && arguments.contains("action") && arguments["action"].is_string()
                ? arguments["action"].get<std::string>()
                : "";
        platform::PowerAction what = platform::PowerAction::Suspend;
        std::string sentence = "Mise en veille.";
        if (action == "shutdown") {
            what = platform::PowerAction::Shutdown;
            sentence = "Extinction de l'ordinateur.";
        } else if (action == "reboot") {
            what = platform::PowerAction::Reboot;
            sentence = "Redémarrage de l'ordinateur.";
        } else if (action != "suspend") {
            return std::unexpected(invalid_arguments("action inconnue : « " + action + " »"));
        }
        if (auto done = platform::power(what); !done) {
            return std::unexpected(execution_error(done.error()));
        }
        return core::ToolOutput{.content = std::move(sentence)};
    }
};

} // namespace

std::vector<std::shared_ptr<const core::Tool>> make_system_control_tools() {
    std::vector<std::shared_ptr<const core::Tool>> tools;
    if (platform::supports_volume()) {
        tools.push_back(std::make_shared<VolumeTool>());
    }
    if (platform::supports_media()) {
        tools.push_back(std::make_shared<MediaTool>());
    }
    if (platform::supports_brightness()) {
        tools.push_back(std::make_shared<BrightnessTool>());
    }
    if (platform::supports_dark_mode()) {
        tools.push_back(std::make_shared<DarkModeTool>());
    }
    if (platform::supports_lock()) {
        tools.push_back(std::make_shared<LockTool>());
    }
    if (platform::supports_notify()) {
        tools.push_back(std::make_shared<NotifyTool>());
    }
    if (platform::supports_power()) {
        tools.push_back(std::make_shared<PowerTool>());
    }
    return tools;
}

core::Result<void> register_system_rules(core::FastRouter& router) {
    const nlohmann::json none = nlohmann::json::object();
    // Règles strictes, phrase entière : en cas de doute, la requête part au LLM.
    const std::vector<std::tuple<std::string, std::string, std::string, nlohmann::json>> rules{
        {R"(^(?:monte|augmente)\s+(?:le\s+)?(?:son|volume)$)", "set_volume", "", {{"change", 10}}},
        {R"(^(?:baisse|diminue)\s+(?:le\s+)?(?:son|volume)$)", "set_volume", "", {{"change", -10}}},
        {R"(^(?:coupe\s+le\s+son|mets\s+en\s+sourdine|sourdine|mute)$)",
         "set_volume",
         "",
         {{"muted", true}}},
        {R"(^(?:remets|réactive|rétablis)\s+le\s+son$)", "set_volume", "", {{"muted", false}}},
        {R"(^(?:mets\s+)?(?:le\s+)?(?:son|volume)\s+(?:à\s+)?(\d{1,3})\s*%?$)", "set_volume",
         "level", none},
        {R"(^(?:mets\s+(?:la\s+musique\s+)?en\s+)?pause$)",
         "media_control",
         "",
         {{"action", "pause"}}},
        {R"(^(?:lecture|reprends(?:\s+la\s+musique)?|relance\s+la\s+musique)$)",
         "media_control",
         "",
         {{"action", "play"}}},
        {R"(^(?:(?:musique|piste|chanson|morceau)\s+suivante?|suivant|passe\s+au\s+suivant)$)",
         "media_control",
         "",
         {{"action", "next"}}},
        {R"(^(?:(?:musique|piste|chanson|morceau)\s+précédente?|précédent)$)",
         "media_control",
         "",
         {{"action", "previous"}}},
        {R"(^(?:qu'est-ce\s+qui\s+(?:passe|joue)|c'est\s+quoi\s+cette\s+musique)\s*\??$)",
         "media_control",
         "",
         {{"action", "now_playing"}}},
        {R"(^(?:mets\s+)?(?:la\s+)?luminosité\s+(?:à\s+)?(\d{1,3})\s*%?$)", "set_brightness",
         "level", none},
        {R"(^(?:monte|augmente)\s+la\s+luminosité$)", "set_brightness", "", {{"change", 10}}},
        {R"(^(?:baisse|diminue)\s+la\s+luminosité$)", "set_brightness", "", {{"change", -10}}},
        {R"(^(?:(?:active|mets|passe\s+en)\s+(?:le\s+)?)?mode\s+sombre$)",
         "set_dark_mode",
         "",
         {{"dark", true}}},
        {R"(^(?:(?:active|mets|passe\s+en)\s+(?:le\s+)?)?mode\s+clair$|^désactive\s+le\s+mode\s+sombre$)",
         "set_dark_mode",
         "",
         {{"dark", false}}},
        {R"(^verr?ouille\s+(?:l'ordinateur|l'écran|la\s+session|le\s+pc|mon\s+ordinateur)$)",
         "lock_screen", "", none},
        {R"(^mets\s+(?:l'ordinateur|le\s+pc|mon\s+ordinateur)\s+en\s+veille$)",
         "power",
         "",
         {{"action", "suspend"}}},
    };
    for (const auto& [pattern, tool, arg, fixed] : rules) {
        if (auto added = router.add_rule(pattern, tool, arg, fixed); !added) {
            return added;
        }
    }
    return {};
}

} // namespace avenoir::tools
