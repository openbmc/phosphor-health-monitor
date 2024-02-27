#pragma once

#include "health_metric.hpp"

namespace phosphor::health::metric::collection
{
namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;

using configs_t = std::vector<ConfigIntf::HealthMetric>;

class HealthMetricCollection
{
  public:
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
    using map_t = std::unordered_map<std::string,
                                     std::unique_ptr<MetricIntf::HealthMetric>>;
    using time_map_t = std::unordered_map<MetricIntf::SubType, uint64_t>;
    /** @brief Create a new health metric collection object */
    void create(const MetricIntf::paths_t& bmcPaths);
    /** @brief Read the CPU */
    auto readCPU() -> bool;
    /** @brief Read the memory */
    auto readMemory() -> bool;
    /** @brief Read the storage */
    auto readStorage() -> bool;
    /** @brief D-Bus bus connection */
    sdbusplus::bus_t& bus;
    /** @brief Metric type */
    MetricIntf::Type type;
    /** @brief Health metric configs */
    const configs_t& configs;
    /** @brief Map of health metrics by subtype */
    map_t metrics;
    /** @brief Map for active time by subtype */
    time_map_t preActiveTime;
    /** @brief Map for total time by subtype */
    time_map_t preTotalTime;
};

} // namespace phosphor::health::metric::collection
