#pragma once

#include "health_metric_config.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/server.hpp>
#include <xyz/openbmc_project/Metrics/Value/server.hpp>

#include <deque>

namespace phosphor
{
namespace health
{
namespace metric
{
using namespace phosphor::health::metric::config;
using AssociationInterface =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;
using ValueInterface = sdbusplus::xyz::openbmc_project::Metrics::server::Value;
using PathInterface =
    sdbusplus::common::xyz::openbmc_project::metrics::Value::namespace_path;
using BmcInterface =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Bmc;
using HealthMetricInterface =
    sdbusplus::server::object_t<ValueInterface, ThresholdInterface,
                                AssociationInterface>;

class HealthMetric : public HealthMetricInterface
{
  public:
    HealthMetric() = delete;
    HealthMetric(const HealthMetric&) = delete;
    // HealthMetric& operator=(const HealthMetric&) = delete;
    HealthMetric(HealthMetric&&) = delete;
    // HealthMetric& operator=(HealthMetric&&) = delete;
    virtual ~HealthMetric() = default;

    HealthMetric(sdbusplus::bus::bus& bus, MetricType metricType,
                 const HealthMetricConfig& metricConfig,
                 const std::vector<std::string>& bmcInventoryPaths) :
        HealthMetricInterface(
            bus, getObjectPath(metricConfig.metricSubtype).c_str()),
        bus(bus), metricType(metricType), metricConfig(metricConfig)
    {
        createHealthMetric(bmcInventoryPaths);
    }

    virtual void updateHealthMetric(double value);

  private:
    void createHealthMetric(const std::vector<std::string>& bmcInventoryPaths);
    void setHealthMetricProperties();
    void checkHealthMetricThreshold(ThresholdInterface::Type type,
                                    ThresholdInterface::Bound bound,
                                    double value);
    void checkHealthMetricThresholds(double value);
    std::string getObjectPath(MetricSubtype metricSubtype);
    /** @brief D-Bus bus connection */
    sdbusplus::bus::bus& bus;
    /** @brief Metric type */
    MetricType metricType;
    /** @brief Metric configuration */
    const HealthMetricConfig& metricConfig;
    /** @brief Window for metric history */
    std::deque<double> metricValueHistory;
};
} // namespace metric
} // namespace health
} // namespace phosphor
