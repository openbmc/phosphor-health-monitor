#include "health_utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::utils
{

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

auto findPaths(sdbusplus::async::context& ctx, const std::string& iface,
               const std::string& subpath) -> sdbusplus::async::task<paths_t>
{
    try
    {
        using ObjectMapper =
            sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

        auto mapper = ObjectMapper(ctx)
                          .service(ObjectMapper::default_service)
                          .path(ObjectMapper::instance_path);

        std::vector<std::string> ifaces = {iface};
        co_return co_await mapper.get_sub_tree_paths(subpath, 0, ifaces);
    }
    catch (std::exception& e)
    {
        error("Exception occurred for GetSubTreePaths for {PATH}: {ERROR}",
              "PATH", subpath, "ERROR", e);
    }
    co_return {};
}

} // namespace phosphor::health::utils
