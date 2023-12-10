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

auto findPaths(sdbusplus::bus_t& bus, const std::string& iface) -> paths_t
{
    sdbusplus::message_t msg = bus.new_method_call(
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths");

    msg.append("/", 0, std::vector<std::string>{iface});

    try
    {
        auto paths = bus.call(msg, int32_t(0)).unpack<paths_t>();
        debug("Found {COUNT} paths for {IFACE}", "COUNT", paths.size(), "IFACE",
              iface);
        return paths;
    }
    catch (std::exception& e)
    {
        error("Exception occurred for GetSubTreePaths for {PATH}: {ERROR}",
              "PATH", inventoryPath, "ERROR", e);
    }
    return {};
}

} // namespace phosphor::health::utils
