#include "health_events.hpp"

#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Metric/Threshold/event.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using MetricUnit = sdbusplus::common::xyz::openbmc_project::metric::Value::Unit;

template <typename errorObj>
auto logAssertThresholdHelper(const std::string& metric, double currentRatio,
                              double thresholdRatio, ThresholdIntf::Type type,
                              ThresholdIntf::Bound bound)
    -> sdbusplus::message::object_path
{
    return lg2::commit(
        errorObj("METRIC_NAME", metric, "READING_VALUE", currentRatio,
                 "THRESHOLD_VALUE", thresholdRatio, "TRIGGER_NAME",
                 to_string(type) + "_" + to_string(bound), "UNITS",
                 MetricUnit::Percent));
}

HealthEvent::HealthEvent(const config::HealthMetric& config)
{
    metric = config.name;
    for (const auto& [key, value] : config.thresholds)
    {
        thresholdLogs[key] = config::ThresholdLog();
    }
}

void HealthEvent::generateThresholdEvent(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentRatio,
    double thresholdRatio, bool assert)
{
    if (assert)
    {
        logAssertThresholds(type, bound, currentRatio, thresholdRatio);
    }
    else
    {
        logDeassertThresholds(type, bound, currentRatio);
    }
}

void HealthEvent::logAssertThresholds(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentRatio,
    double thresholdRatio)
{
    namespace errors =
        sdbusplus::error::xyz::openbmc_project::metric::Threshold;
    static const std::map<std::tuple<ThresholdIntf::Type, ThresholdIntf::Bound>,
                          std::function<sdbusplus::message::object_path(
                              const std::string&, double, double,
                              ThresholdIntf::Type, ThresholdIntf::Bound)>>
        thresholdLogMap = {
            {{ThresholdIntf::Type::HardShutdown, ThresholdIntf::Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerCritical>},
            {{ThresholdIntf::Type::HardShutdown, ThresholdIntf::Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperCritical>},
            {{ThresholdIntf::Type::SoftShutdown, ThresholdIntf::Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerCritical>},
            {{ThresholdIntf::Type::SoftShutdown, ThresholdIntf::Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperCritical>},
            {{ThresholdIntf::Type::PerformanceLoss,
              ThresholdIntf::Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerWarning>},
            {{ThresholdIntf::Type::PerformanceLoss,
              ThresholdIntf::Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperWarning>},
            {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerCritical>},
            {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperCritical>},
            {{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerWarning>},
            {{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperWarning>}};
    auto& thresholdLog = thresholdLogs.at({type, bound});
    if (thresholdLog.assertedLog)
    {
        // Technically we should never get here. But handle anyway.
        lg2::error("Ignoring new log with unresolved outstanding entry: {LOG}",
                   "LOG", std::string(*(thresholdLog.assertedLog)));
        return;
    }
    try
    {
        thresholdLog.assertedLog = thresholdLogMap.at(
            {type, bound})(metric, currentRatio, thresholdRatio, type, bound);
    }
    catch (std::out_of_range& e)
    {
        lg2::error(
            "Could not create threshold log entry for {METRIC}, value out of range",
            "METRIC", metric);
    }
    catch (std::exception& e)
    {
        lg2::error("Could not create threshold log entry for {METRIC}",
                   "METRIC", metric);
    }
}

void HealthEvent::logDeassertThresholds(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentRatio)
{
    namespace events =
        sdbusplus::event::xyz::openbmc_project::metric::Threshold;

    auto& thresholdLog = thresholdLogs.at({type, bound});
    if (thresholdLog.assertedLog)
    {
        try
        {
            lg2::resolve(*thresholdLog.assertedLog);
        }
        catch (std::exception& ec)
        {
            lg2::error("Unable to resolve {LOG} : {ERROR}", "LOG",
                       std::string(*thresholdLog.assertedLog), "ERROR",
                       ec.what());
        }
        error("DEASSERT: {METRIC} {CUR}", "METRIC", metric, "CUR",
              currentRatio);
        thresholdLog.assertedLog.reset();
    }
    auto it =
        std::find_if(thresholdLogs.begin(), thresholdLogs.end(), [](auto& th) {
            return th.second.assertedLog.has_value();
        });
    // Return if there are outstanding asserts.
    if (it != thresholdLogs.end())
    {
        return;
    }
    lg2::commit(events::TriggerNumericReadingNormal(
        "METRIC_NAME", metric, "READING_VALUE", currentRatio, "TRIGGER_NAME",
        to_string(type) + "_" + to_string(bound), "UNITS",
        MetricUnit::Percent));
}

} // namespace phosphor::health::metric
