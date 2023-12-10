#pragma once

#include "health_metric_config.hpp"
#include "health_utils.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/server.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <deque>
#include <tuple>

namespace phosphor::health::metric
{

using phosphor::health::utils::findPaths;
using phosphor::health::utils::paths_t;
using phosphor::health::utils::startUnit;
using AssociationIntf =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;
using ValueIntf = sdbusplus::xyz::openbmc_project::Metric::server::Value;
using PathIntf =
    sdbusplus::common::xyz::openbmc_project::metric::Value::namespace_path;
using BmcIntf = sdbusplus::xyz::openbmc_project::Inventory::Item::server::Bmc;
using MetricIntf =
    sdbusplus::server::object_t<ValueIntf, ThresholdIntf, AssociationIntf>;

class HealthMetric : public MetricIntf
{
  public:
    /** @brief Tuple of (absolute, percentage) values */
    using value_t = std::tuple<double, double>;
    HealthMetric() = delete;
    HealthMetric(const HealthMetric&) = delete;
    HealthMetric(HealthMetric&&) = delete;
    virtual ~HealthMetric() = default;

    HealthMetric(sdbusplus::bus::bus& bus, phosphor::health::metric::Type type,
                 const config::HealthMetric config, const paths_t& bmcPaths) :
        MetricIntf(bus, getPath(config.subType).c_str(), action::defer_emit),
        bus(bus), type(type), config(config)
    {
        create(bmcPaths);
        this->emit_object_added();
    }

    /** @brief Update the health metric with the given value */
    void update(value_t value);

  private:
    /** @brief Create a new health metric object */
    void create(const paths_t& bmcPaths);
    /** @brief Set properties on the health metric object */
    void setProperties();
    /** @brief Check specified threshold for the given value */
    void checkThreshold(ThresholdIntf::Type type, ThresholdIntf::Bound bound,
                        double value);
    /** @brief Check all thresholds for the given value */
    void checkThresholds(double value);
    /** @brief Get the object path for the given subtype */
    auto getPath(SubType subType) -> std::string;
    /** @brief D-Bus bus connection */
    sdbusplus::bus::bus& bus;
    /** @brief Metric type */
    phosphor::health::metric::Type type;
    /** @brief Metric configuration */
    const config::HealthMetric config;
    /** @brief Window for metric history */
    std::deque<double> history;
};

} // namespace phosphor::health::metric
