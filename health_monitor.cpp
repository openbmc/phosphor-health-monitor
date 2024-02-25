#include "health_monitor.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/common.hpp>
#include <xyz/openbmc_project/Inventory/Item/common.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::monitor
{

using namespace phosphor::health::utils;

auto HealthMonitor::startup() -> sdbusplus::async::task<>
{
    info("Creating Health Monitor with config size {SIZE}", "SIZE",
         configs.size());

    static constexpr auto bmcIntf = sdbusplus::common::xyz::openbmc_project::
        inventory::item::Bmc::interface;
    static constexpr auto invPath = sdbusplus::common::xyz::openbmc_project::
        inventory::Item::namespace_path;
    auto bmcPaths = co_await findPaths(ctx, bmcIntf, invPath);

    for (auto& [type, collectionConfig] : configs)
    {
        info("Creating Health Metric Collection for {TYPE}", "TYPE",
             std::to_underlying(type));
        collections[type] =
            std::make_unique<CollectionIntf::HealthMetricCollection>(
                ctx.get_bus(), type, collectionConfig, bmcPaths);
    }

    co_await run();
}

auto HealthMonitor::run() -> sdbusplus::async::task<>
{
    info("Running Health Monitor");
    while (!ctx.stop_requested())
    {
        for (auto& [type, collection] : collections)
        {
            debug("Reading Health Metric Collection for {TYPE}", "TYPE",
                  std::to_underlying(type));
            collection->read();
        }
        co_await sdbusplus::async::sleep_for(ctx,
                                             std::chrono::milliseconds(1000));
    }
}

} // namespace phosphor::health::monitor

using namespace phosphor::health::monitor;

int main()
{
    constexpr auto path = MetricIntf::ValueIntf::Value::namespace_path::value;
    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager{ctx, path};
    constexpr auto healthMonitorServiceName = "xyz.openbmc_project.HealthMon";

    info("Creating health monitor");
    HealthMonitor healthMonitor{ctx};
    ctx.request_name(healthMonitorServiceName);

    ctx.run();
    return 0;
}
