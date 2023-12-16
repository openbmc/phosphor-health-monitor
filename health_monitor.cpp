#include "health_monitor.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::monitor
{

using namespace phosphor::health::utils;

void HealthMonitor::create()
{
    info("Creating Health Monitor with config size {SIZE}", "SIZE",
         configs.size());
    constexpr auto BMCInventoryItem = "xyz.openbmc_project.Inventory.Item.Bmc";
    auto bmcPaths = findPaths(bus, BMCInventoryItem);

    for (auto& [type, collectionConfig] : configs)
    {
        info("Creating Health Metric Collection for {TYPE}", "TYPE",
             std::to_underlying(type));
        collections[type] =
            std::make_unique<CollectionIntf::HealthMetricCollection>(
                bus, type, collectionConfig, bmcPaths);
    }
}

void HealthMonitor::run()
{
    info("Running Health Monitor");
    for (auto& [type, collection] : collections)
    {
        debug("Reading Health Metric Collection for {TYPE}", "TYPE",
              std::to_underlying(type));
        collection->read();
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
    HealthMonitor healthMonitor{ctx.get_bus()};
    ctx.request_name(healthMonitorServiceName);

    ctx.spawn([&]() -> sdbusplus::async::task<> {
        while (!ctx.stop_requested())
        {
            healthMonitor.run();
            co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(5));
        }
    }());

    ctx.run();
    return 0;
}
