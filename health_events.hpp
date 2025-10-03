#pragma once

#include "health_metric_config.hpp"

#include <xyz/openbmc_project/Metric/Value/server.hpp>

namespace phosphor::health::metric
{

template <typename errorObj>
auto logAssertThresholdHelper(const std::string& metric, double currentRatio,
                              double thresholdRatio, ThresholdIntf::Type type,
                              ThresholdIntf::Bound bound)
    -> sdbusplus::message::object_path;

class HealthEvent
{
  public:
    HealthEvent() = delete;
    HealthEvent(const HealthEvent&) = delete;
    HealthEvent(HealthEvent&&) = delete;
    virtual ~HealthEvent() = default;

    HealthEvent(const config::HealthMetric& config);
    
    void generateThresholdEvent(ThresholdIntf::Type type,
                                ThresholdIntf::Bound bound, double currentRatio,
                                double thresholdRatio, bool assert);

  private:
    std::string metric = "unnamed";
    config::ThresholdLog::map_t thresholdLogs{};
    virtual void logAssertThresholds(
        ThresholdIntf::Type type, ThresholdIntf::Bound bound,
        double currentRatio, double thresholdRatio);
    virtual void logDeassertThresholds(ThresholdIntf::Type type,
                                       ThresholdIntf::Bound bound,
                                       double currentRatio);
    friend class HealthEventCI;
};

} // namespace phosphor::health::metric
