#include <array>
#include <chrono>
#include <exception>
#include <memory>
#include <string>

#include "avenoir/tools/tools.hpp"

#include "tools_impl.hpp"

namespace avenoir::tools {

namespace detail {

std::string time_sentence(std::chrono::local_seconds time) {
    // Indexés par weekday::c_encoding() (0 = dimanche) et month (1 = janvier).
    static constexpr std::array<std::string_view, 7> kDays = {
        "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
    static constexpr std::array<std::string_view, 12> kMonths = {
        "janvier", "février", "mars",      "avril",   "mai",      "juin",
        "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};

    const auto days = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::year_month_day date{days};
    const std::chrono::weekday weekday{days};
    const std::chrono::hh_mm_ss clock{time - days};

    const auto day = static_cast<unsigned>(date.day());
    const auto minutes = clock.minutes().count();
    std::string sentence = "Il est " + std::to_string(clock.hours().count()) + " h " +
                           (minutes < 10 ? "0" : "") + std::to_string(minutes) + ", ";
    sentence += kDays.at(weekday.c_encoding());
    sentence += " " + (day == 1 ? std::string("1er") : std::to_string(day)) + " ";
    sentence += kMonths.at(static_cast<unsigned>(date.month()) - 1);
    sentence += " " + std::to_string(static_cast<int>(date.year())) + ".";
    return sentence;
}

} // namespace detail

namespace {

class CurrentTimeTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "current_time",
            .description = "Donne la date et l'heure locales actuelles.",
            .input_schema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .risk = core::RiskLevel::ReadOnly,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& /*arguments*/) const override {
        try {
            const std::chrono::zoned_time now{std::chrono::current_zone(),
                                              std::chrono::system_clock::now()};
            const auto local = std::chrono::floor<std::chrono::seconds>(now.get_local_time());
            return core::ToolOutput{.content = detail::time_sentence(local)};
        } catch (const std::exception& e) {
            // Base des fuseaux horaires introuvable ou illisible.
            return std::unexpected(
                core::ToolError{.kind = core::ToolErrorKind::Execution,
                                .message = std::string("fuseau horaire : ") + e.what()});
        }
    }
};

} // namespace

std::shared_ptr<const core::Tool> make_current_time_tool() {
    return std::make_shared<CurrentTimeTool>();
}

} // namespace avenoir::tools
