#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "avenoir/tools/tools.hpp"

#include "platform.hpp"
#include "tools_impl.hpp"

namespace avenoir::tools {

namespace detail {

std::string system_sentence(const SystemFacts& facts) {
    std::string sentence = "Système : ";
    if (facts.distribution) {
        sentence += *facts.distribution + " (" + facts.os + ", " + facts.arch + ")";
    } else {
        sentence += facts.os + " (" + facts.arch + ")";
    }
    if (facts.hostname) {
        sentence += ", machine « " + *facts.hostname + " »";
    }
    if (facts.cpus) {
        sentence += ", " + std::to_string(*facts.cpus) + (*facts.cpus > 1 ? " cœurs" : " cœur");
    }
    return sentence + ".";
}

} // namespace detail

namespace {

std::string_view architecture() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "architecture inconnue";
#endif
}

class SystemInfoTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "system_info",
            .description = "Donne des informations sur l'ordinateur : système d'exploitation, "
                           "distribution, architecture, nom de la machine, nombre de cœurs.",
            .input_schema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .risk = core::RiskLevel::ReadOnly,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& /*arguments*/) const override {
        const unsigned cpus = std::thread::hardware_concurrency();
        const detail::SystemFacts facts{
            .os = std::string(platform::os_name()),
            .distribution = platform::distribution(),
            .arch = std::string(architecture()),
            .hostname = platform::hostname(),
            .cpus = cpus > 0 ? std::optional<std::size_t>(cpus) : std::nullopt,
        };
        return core::ToolOutput{.content = detail::system_sentence(facts)};
    }
};

} // namespace

std::shared_ptr<const core::Tool> make_system_info_tool() {
    return std::make_shared<SystemInfoTool>();
}

} // namespace avenoir::tools
