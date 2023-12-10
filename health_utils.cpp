#include "health_utils.hpp"

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::utils
{

static const char* inventoryPath = "/xyz/openbmc_project/inventory";

void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit)
{
    if (sysdUnit.empty())
    {
        return;
    }
    sdbusplus::message_t msg = bus.new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    msg.append(sysdUnit, "replace");
    bus.call_noreply(msg);
}

} // namespace phosphor::health::utils
