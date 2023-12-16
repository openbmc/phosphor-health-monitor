#pragma once

#include "health_metric_collection.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Common/Threshold/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/server.hpp>
#include <xyz/openbmc_project/Metrics/Value/server.hpp>

#include <map>

namespace phosphor
{
namespace health
{
namespace monitor
{
using namespace phosphor::health::metric::config;
using namespace phosphor::health::metric::collection;
class HealthMonitor
{
  public:
    HealthMonitor() = delete;
    virtual ~HealthMonitor() = default;

    HealthMonitor(sdbusplus::bus_t& bus) :
        bus(bus), healthMetricConfigs(getHealthMetricConfigs())
    {
        createHealthMonitor();
    }

    bool runHealthMonitor();

  private:
    sdbusplus::bus_t& bus;
    void createHealthMonitor();
    std::map<MetricType, std::vector<HealthMetricConfig>> healthMetricConfigs;
    std::map<MetricType, std::unique_ptr<HealthMetricCollection>>
        healthMetricCollections;
};
} // namespace monitor
} // namespace health
} // namespace phosphor
