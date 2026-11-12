#include <memory>

#include "avenoir/core/error.hpp"
#include "avenoir/ipc/transport.hpp"

namespace avenoir::ipc {

core::Result<Endpoint> default_endpoint() {
    return core::make_error(core::ErrorCode::Ipc, "démon non pris en charge sur ce système");
}

core::Result<std::unique_ptr<Listener>> listen(const Endpoint& /*endpoint*/) {
    return core::make_error(core::ErrorCode::Ipc, "démon non pris en charge sur ce système");
}

core::Result<std::unique_ptr<Connection>> connect(const Endpoint& /*endpoint*/) {
    return core::make_error(core::ErrorCode::Ipc, "démon non pris en charge sur ce système");
}

} // namespace avenoir::ipc
