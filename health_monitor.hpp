#pragma once

#include "health_metric_collection.hpp"

#include <sdbusplus/async.hpp>

#include <unordered_map>

namespace phosphor::health::monitor
{
namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;
namespace CollectionIntf = phosphor::health::metric::collection;
class HealthMonitor
{
  public:
    HealthMonitor() = delete;

    explicit HealthMonitor(sdbusplus::async::context& ctx) :
        ctx(ctx), configs(ConfigIntf::getHealthMetricConfigs())
    {
        ctx.spawn(startup());
    }

  private:
    /** @brief Setup and run a new health monitor object */
    auto startup() -> sdbusplus::async::task<>;
    /** @brief Run the health monitor */
    auto run() -> sdbusplus::async::task<>;

    using map_t = std::unordered_map<
        MetricIntf::Type,
        std::unique_ptr<CollectionIntf::HealthMetricCollection>>;

    /** @brief D-Bus context */
    sdbusplus::async::context& ctx;
    /** @brief Health metric configs */
    ConfigIntf::HealthMetric::map_t configs;
    map_t collections;
};

} // namespace phosphor::health::monitor
