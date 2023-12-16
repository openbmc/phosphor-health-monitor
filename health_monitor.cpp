#include "health_monitor.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::monitor
{

using namespace phosphor::health::utils;

void HealthMonitor::create()
{
    info("Creating Health Monitor with config size {CSIZE}", "CSIZE",
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

auto HealthMonitor::run() -> bool
{
    info("Running Health Monitor");
    for (auto& [type, collection] : collections)
    {
        info("Reading Health Metric Collection for {TYPE}", "TYPE",
             std::to_underlying(type));
        collection->read();
    }

    return true;
}

} // namespace phosphor::health::monitor

using namespace phosphor::health::monitor;

int main()
{
    constexpr auto path = MetricIntf::ValueIntf::Value::namespace_path::value;
    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager{ctx, path};
    constexpr auto healthMonitorServiceName = "xyz.openbmc_project.HealthMon";
    ctx.request_name(healthMonitorServiceName);

    info("Creating health monitor");
    HealthMonitor healthMonitor{ctx.get_bus()};

    ctx.spawn([](sdbusplus::async::context& ctx,
                 HealthMonitor& healthMonitor) -> sdbusplus::async::task<> {
        while (true)
        {
            healthMonitor.run();
            co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds(5));
        }
    }(ctx, healthMonitor));

    ctx.run();
    return 0;
}
