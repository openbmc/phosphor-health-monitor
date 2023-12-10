#include "health_metric.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/sdbus.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor
{
namespace health
{
namespace utils
{

const char* InventoryPath = "/xyz/openbmc_project/inventory";

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

std::vector<std::string> findPathsWithType(sdbusplus::bus_t& bus,
                                           const std::string& iface)
{
    std::vector<std::string> ret;

    sdbusplus::message_t msg = bus.new_method_call(
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths");

    msg.append("/", 0, std::vector<std::string>{iface});

    try
    {
        bus.call(msg, 0).read(ret);
        if (!ret.empty())
        {
            debug("{IFACE} found", "IFACE", iface);
        }
        else
        {
            debug("{IFACE} not found", "IFACE", iface);
        }
    }
    catch (std::exception& e)
    {
        error("Exception occurred while calling {PATH}: {ERROR}", "PATH",
              InventoryPath, "ERROR", e);
    }
    return ret;
}

} // namespace utils
} // namespace health
} // namespace phosphor
