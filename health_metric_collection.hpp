#pragma once

#include "health_metric.hpp"

namespace phosphor
{
namespace health
{
namespace metric
{
namespace collection
{
using namespace phosphor::health::metric::config;
using namespace phosphor::health::metric;
using HealthMetricCache = std::unordered_map<MetricSubtype, HealthMetricValue>;

class HealthMetricCollection
{
  public:
    virtual ~HealthMetricCollection() = default;
    HealthMetricCollection(
        sdbusplus::bus_t& bus, MetricType metricType,
        const std::vector<HealthMetricConfig>& healthMetricConfigs,
        const std::vector<std::string>& bmcInventoryPaths) :
        bus(bus),
        metricType(metricType), healthMetricConfigs(healthMetricConfigs)
    {
        createHealthMetricCollection(bmcInventoryPaths);
    }

    void readHealthMetricCollection();

  private:
    /** @brief D-Bus bus connection */
    sdbusplus::bus_t& bus;
    void createHealthMetricCollection(
        const std::vector<std::string>& bmcInventoryPaths);
    MetricType metricType;
    const std::vector<HealthMetricConfig>& healthMetricConfigs;
    std::unordered_map<MetricSubtype, std::unique_ptr<HealthMetric>>
        healthMetrics;
    HealthMetricCache healthMetricCache;
};

} // namespace collection
} // namespace metric
} // namespace health
} // namespace phosphor
