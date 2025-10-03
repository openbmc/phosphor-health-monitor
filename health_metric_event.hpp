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

    explicit HealthMetricEvent(const config::HealthMetric& config);

    /** @brief Generate the threshold assert/deassert event */
    void generateThresholdEvent(
        ThresholdIntf::Type type, ThresholdIntf::Bound bound,
        double currentPercent, double thresholdPercent, bool assert);

  private:
    void logAssertThresholds(ThresholdIntf::Type type,
                             ThresholdIntf::Bound bound, double currentPercent,
                             double thresholdPercent);
    void logDeassertThresholds(ThresholdIntf::Type type,
                               ThresholdIntf::Bound bound,
                               double currentPercent);

    std::string metric = "unknown";
    config::ThresholdLog::map_t thresholdLogs{};
};

} // namespace phosphor::health::metric
