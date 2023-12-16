#pragma once

#include "health_metric_collection.hpp"

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

    HealthMonitor(sdbusplus::bus_t& bus) :
        bus(bus), configs(ConfigIntf::getHealthMetricConfigs())
    {
        create();
    }

    /** @brief Run the health monitor */
    auto run() -> bool;

  private:
    using map_t = std::unordered_map<
        MetricIntf::Type,
        std::unique_ptr<CollectionIntf::HealthMetricCollection>>;
    /** @brief Create a new health monitor object */
    void create();
    /** @brief D-Bus bus connection */
    sdbusplus::bus_t& bus;
    /** @brief Health metric configs */
    ConfigIntf::HealthMetric::map_t configs;
    map_t collections;
};

} // namespace phosphor::health::monitor
