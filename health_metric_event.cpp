#include "health_metric_event.hpp"

#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Metric/Threshold/event.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <tuple>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using MetricUnit = sdbusplus::common::xyz::openbmc_project::metric::Value::Unit;

namespace
{

template <typename errorObj>
auto logAssertThresholdHelper(const std::string& metric, double currentPercent,
                              double thresholdPercent, ThresholdIntf::Type type,
                              ThresholdIntf::Bound bound)
    -> sdbusplus::object_path
{
    return lg2::commit(
        errorObj("METRIC_NAME", metric, "READING_VALUE", currentPercent,
                 "THRESHOLD_VALUE", thresholdPercent, "TRIGGER_NAME",
                 to_string(type) + "_" + to_string(bound), "UNITS",
                 MetricUnit::Percent));
}

} // namespace

HealthMetricEvent::HealthMetricEvent(const config::HealthMetric& config)
{
    metric = config.name;
    for (const auto& [key, value] : config.thresholds)
    {
        thresholdLogs[key] = config::ThresholdLog();
    }
}

void HealthMetricEvent::generateThresholdEvent(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentPercent,
    double thresholdPercent, bool assert)
{
    if (assert)
    {
        logAssertThresholds(type, bound, currentPercent, thresholdPercent);
    }
    else
    {
        logDeassertThresholds(type, bound, currentPercent);
    }
}

auto HealthMetricEvent::commitAssertLog(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentPercent,
    double thresholdPercent) -> sdbusplus::object_path
{
    namespace errors =
        sdbusplus::error::xyz::openbmc_project::metric::Threshold;
    static const std::map<std::tuple<ThresholdIntf::Type, ThresholdIntf::Bound>,
                          std::function<sdbusplus::object_path(
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

    return thresholdLogMap.at(
        {type, bound})(metric, currentPercent, thresholdPercent, type, bound);
}

void HealthMetricEvent::resolveAssertLog(const sdbusplus::object_path& path)
{
    lg2::resolve(path);
}

void HealthMetricEvent::commitNormalLog(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentPercent)
{
    namespace events =
        sdbusplus::event::xyz::openbmc_project::metric::Threshold;

    lg2::commit(events::TriggerNumericReadingNormal(
        "METRIC_NAME", metric, "READING_VALUE", currentPercent, "TRIGGER_NAME",
        to_string(type) + "_" + to_string(bound), "UNITS",
        MetricUnit::Percent));
}

void HealthMetricEvent::logAssertThresholds(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentPercent,
    double thresholdPercent)
{
    auto& thresholdLog = thresholdLogs.at({type, bound});
    if (thresholdLog.assertedLog)
    {
        // Technically we should never get here. But handle anyway.
        error("Ignoring new log with unresolved outstanding entry: {LOG}",
              "LOG", std::string(*(thresholdLog.assertedLog)));
        return;
    }
    try
    {
        thresholdLog.assertedLog =
            commitAssertLog(type, bound, currentPercent, thresholdPercent);
    }
    catch (std::out_of_range& e)
    {
        error(
            "Could not create threshold log entry for {METRIC}, value out of range",
            "METRIC", metric);
    }
    catch (std::exception& e)
    {
        error("Could not create threshold log entry for {METRIC}: {ERROR}",
              "METRIC", metric, "ERROR", e.what());
    }
}

void HealthMetricEvent::logDeassertThresholds(
    ThresholdIntf::Type type, ThresholdIntf::Bound bound, double currentPercent)
{
    auto& thresholdLog = thresholdLogs.at({type, bound});
    if (thresholdLog.assertedLog)
    {
        try
        {
            resolveAssertLog(*thresholdLog.assertedLog);
        }
        catch (std::exception& e)
        {
            error("Unable to resolve {LOG} : {ERROR}", "LOG",
                  std::string(*thresholdLog.assertedLog), "ERROR", e.what());
        }
        error("DEASSERT: {METRIC} {CURRENT_PERCENT}", "METRIC", metric,
              "CURRENT_PERCENT", currentPercent);
        thresholdLog.assertedLog.reset();
    }
    auto it = std::find_if(
        thresholdLogs.begin(), thresholdLogs.end(),
        [](const auto& th) { return th.second.assertedLog.has_value(); });
    // Return if there are outstanding asserts.
    if (it != thresholdLogs.end())
    {
        return;
    }
    try
    {
        commitNormalLog(type, bound, currentPercent);
    }
    catch (std::exception& e)
    {
        error("Could not create reading normal log entry for {METRIC}: {ERROR}",
              "METRIC", metric, "ERROR", e.what());
    }
}

} // namespace phosphor::health::metric
