#include "health_monitor.hpp"

#include "health_utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>

PHOSPHOR_LOG2_USING;

constexpr auto BMCInventoryItem = "xyz.openbmc_project.Inventory.Item.Bmc";
constexpr auto healthMonitorServiceName =
    "xyz.openbmc_project.bmc_health_monitor";

namespace phosphor::health::monitor
{

using namespace phosphor::health::utils;
using ValueInterface = sdbusplus::xyz::openbmc_project::Metrics::server::Value;

void HealthMonitor::createHealthMonitor()
{
    info("Creating Health Monitor with config size {CONFIG_SIZE}",
         "CONFIG_SIZE", healthMetricConfigs.size());
    std::vector<std::string> bmcInventoryPaths =
        findPathsWithType(bus, BMCInventoryItem);

    for (auto& [metricType, healthMetricConfig] : healthMetricConfigs)
    {
        info("Creating Health Metric Collection for {METRIC_TYPE}",
             "METRIC_TYPE", std::to_underlying(metricType));
        healthMetricCollections[metricType] =
            std::make_unique<HealthMetricCollection>(
                bus, metricType, healthMetricConfig, bmcInventoryPaths);
    }
}

bool HealthMonitor::runHealthMonitor()
{
    info("Running Health Monitor");
    for (auto& [metricType, healthMetricCollection] : healthMetricCollections)
    {
        info("Reading Health Metric Collection for {METRIC_TYPE}",
             "METRIC_TYPE", std::to_underlying(metricType));
        healthMetricCollection->readHealthMetricCollection();
    }

    return true;
}

int startMonitor()
{
    constexpr auto path = ValueInterface::Value::namespace_path::value;
    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager{ctx, path};
    ctx.get_bus().request_name(healthMonitorServiceName);

    HealthMonitor healthMonitor{ctx.get_bus()};

    ctx.spawn([](sdbusplus::async::context& ctx,
                 HealthMonitor& healthMonitor) -> sdbusplus::async::task<> {
        while (true)
        {
            healthMonitor.runHealthMonitor();
            co_await sdbusplus::async::sleep_for(
                ctx, std::chrono::milliseconds(5 * 1000));
        }
    }(ctx, healthMonitor));

    ctx.run();
    return 0;
}

} // namespace phosphor::health::monitor
