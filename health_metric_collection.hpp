#pragma once

#include "health_metric.hpp"

namespace phosphor::health::metric::collection
{
namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;

using configs_t = std::vector<ConfigIntf::HealthMetric>;
using cache_t =
    std::unordered_map<MetricIntf::SubType, MetricIntf::HealthMetric::value_t>;

class HealthMetricCollection
{
  public:
    virtual ~HealthMetricCollection() = default;
    HealthMetricCollection(sdbusplus::bus_t& bus, MetricIntf::Type type,
                           const configs_t& configs,
                           MetricIntf::paths_t& bmcPaths) :
        bus(bus),
        type(type), configs(configs)
    {
        create(bmcPaths);
    }

    /** @brief Read the health metric collection from the system */
    void read();

  private:
    using map_t = std::unordered_map<MetricIntf::SubType,
                                     std::unique_ptr<MetricIntf::HealthMetric>>;
    /** @brief Create a new health metric collection object */
    void create(const MetricIntf::paths_t& bmcPaths);
    /** @brief D-Bus bus connection */
    sdbusplus::bus_t& bus;
    /** @brief Metric type */
    MetricIntf::Type type;
    /** @brief Health metric configs */
    const configs_t& configs;
    /** @brief Map of health metrics by subtype */
    map_t metrics;
    /** @brief Cache of last read values */
    cache_t cache;
};

} // namespace phosphor::health::metric::collection
