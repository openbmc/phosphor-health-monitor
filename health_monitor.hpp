#pragma once

#include "health_metric_collection.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Common/Threshold/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/server.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <map>

namespace phosphor::health::monitor
{
namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;
namespace CollectionIntf = phosphor::health::metric::collection;
class HealthMonitor
{
  public:
    HealthMonitor() = delete;
    virtual ~HealthMonitor() = default;

    HealthMonitor(sdbusplus::bus_t& bus) :
        bus(bus), configs(ConfigIntf::getHealthMetricConfigs())
    {
        create();
    }

    /** @brief Run the health monitor */
    auto run() -> bool;

  private:
    using map_t =
        std::map<MetricIntf::Type,
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
