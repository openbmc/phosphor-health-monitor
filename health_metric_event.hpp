#pragma once

#include "health_metric_config.hpp"

#include <sdbusplus/message/native_types.hpp>

#include <string>

namespace phosphor::health::metric
{

class HealthMetricEvent
{
  public:
    HealthMetricEvent() = delete;
    HealthMetricEvent(const HealthMetricEvent&) = delete;
    HealthMetricEvent(HealthMetricEvent&&) = delete;
    ~HealthMetricEvent() = default;

    HealthMetricEvent(const config::HealthMetric& config,
                      const std::string& path);

    /** @brief Generate the threshold assert/deassert event */
    void generateThresholdEvent(ThresholdIntf::Type type,
                                ThresholdIntf::Bound bound, double currentValue,
                                double thresholdValue, MetricUnit unit,
                                bool assert);

    /** @brief Generate the reading normal event, once all the threshold
     *         assertions have been cleared */
    void generateNormalEvent(double currentValue, MetricUnit unit);

  private:
    void logAssertThresholds(ThresholdIntf::Type type,
                             ThresholdIntf::Bound bound, double currentValue,
                             double thresholdValue, MetricUnit unit);
    void logDeassertThresholds(ThresholdIntf::Type type,
                               ThresholdIntf::Bound bound);

    std::string metric = "unknown";
    /** @brief The object path of the metric */
    sdbusplus::object_path metricPath;
    config::ThresholdLog::map_t thresholdLogs{};
};

} // namespace phosphor::health::metric
