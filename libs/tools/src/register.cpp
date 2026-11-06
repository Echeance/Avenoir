#include "avenoir/tools/tools.hpp"

#include "platform.hpp"
#include "tools_impl.hpp"

namespace avenoir::tools {

core::Result<void> register_native_tools(core::ToolRegistry& registry) {
    if (auto added = registry.add(make_current_time_tool()); !added) {
        return added;
    }
    if (auto added = registry.add(make_system_info_tool()); !added) {
        return added;
    }
    // Un outil indisponible sur cet OS n'est pas enregistré : le LLM ne doit
    // pas voir un outil inutilisable (PROJECT.md § 4.3).
    if (platform::supports_launching()) {
        if (auto added = registry.add(make_launch_app_tool()); !added) {
            return added;
        }
        if (auto added = registry.add(make_open_url_tool()); !added) {
            return added;
        }
    }
    return {};
}

} // namespace avenoir::tools
