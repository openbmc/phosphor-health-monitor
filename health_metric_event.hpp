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

    /** @brief Generate the reading normal event, once all the threshold
     *         assertions have been cleared */
    void generateNormalEvent(ThresholdIntf::Type type,
                             ThresholdIntf::Bound bound, double currentPercent);

  private:
    void logAssertThresholds(ThresholdIntf::Type type,
                             ThresholdIntf::Bound bound, double currentPercent,
                             double thresholdPercent);
    void logDeassertThresholds(ThresholdIntf::Type type,
                               ThresholdIntf::Bound bound,
                               double currentPercent);

    std::string metric = "unknown";
    config::ThresholdLog::map_t thresholdLogs{};
    /** @brief Whether a threshold log was created since the reading was last
     *         normal */
    bool normalLogPending = false;
};

} // namespace phosphor::health::metric
