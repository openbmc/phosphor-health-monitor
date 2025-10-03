#include "health_metric.hpp"

#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Metric/Threshold/event.hpp>

#include <cmath>
#include <numeric>
#include <unordered_map>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using association_t = std::tuple<std::string, std::string, std::string>;

auto HealthMetric::getPath(MType type, std::string name, SubType subType)
    -> std::string
{
    std::string path;
    switch (subType)
    {
        case SubType::cpuTotal:
        {
            return std::string(BmcPath) + "/" + PathIntf::total_cpu;
        }
        case SubType::cpuKernel:
        {
            return std::string(BmcPath) + "/" + PathIntf::kernel_cpu;
        }
        case SubType::cpuUser:
        {
            return std::string(BmcPath) + "/" + PathIntf::user_cpu;
        }
        case SubType::memoryAvailable:
        {
            return std::string(BmcPath) + "/" + PathIntf::available_memory;
        }
        case SubType::memoryBufferedAndCached:
        {
            return std::string(BmcPath) + "/" +
                   PathIntf::buffered_and_cached_memory;
        }
        case SubType::memoryFree:
        {
            return std::string(BmcPath) + "/" + PathIntf::free_memory;
        }
        case SubType::memoryShared:
        {
            return std::string(BmcPath) + "/" + PathIntf::shared_memory;
        }
        case SubType::memoryTotal:
        {
            return std::string(BmcPath) + "/" + PathIntf::total_memory;
        }
        case SubType::NA:
        {
            if (type == MType::storage)
            {
                static constexpr auto nameDelimiter = "_";
                auto storageType = name.substr(
                    name.find_last_of(nameDelimiter) + 1, name.length());
                std::ranges::for_each(storageType, [](auto& c) {
                    c = std::tolower(c);
                });
                return std::string(BmcPath) + "/" + PathIntf::storage + "/" +
                       storageType;
            }
            else
            {
                error("Invalid metric {SUBTYPE} for metric {TYPE}", "SUBTYPE",
                      subType, "TYPE", type);
                return "";
            }
        }
        default:
        {
            error("Invalid metric {SUBTYPE}", "SUBTYPE", subType);
            return "";
        }
    }
}

void HealthMetric::initProperties()
{
    switch (type)
    {
        case MType::cpu:
        {
            ValueIntf::unit(ValueIntf::Unit::Percent, true);
            ValueIntf::minValue(0.0, true);
            ValueIntf::maxValue(100.0, true);
            break;
        }
        case MType::memory:
        case MType::storage:
        {
            ValueIntf::unit(ValueIntf::Unit::Bytes, true);
            ValueIntf::minValue(0.0, true);
            break;
        }
        case MType::inode:
        case MType::unknown:
        default:
        {
            throw std::invalid_argument("Invalid metric type");
        }
    }
    ValueIntf::value(std::numeric_limits<double>::quiet_NaN(), true);

    using bound_map_t = std::map<Bound, double>;
    std::map<Type, bound_map_t> thresholds;
    for (const auto& [key, value] : config.thresholds)
    {
        auto type = std::get<Type>(key);
        auto bound = std::get<Bound>(key);
        auto threshold = thresholds.find(type);
        if (threshold == thresholds.end())
        {
            bound_map_t bounds;
            bounds.emplace(bound, std::numeric_limits<double>::quiet_NaN());
            thresholds.emplace(type, bounds);
        }
        else
        {
            threshold->second.emplace(bound, value.value);
        }
    }
    ThresholdIntf::value(thresholds, true);
}

bool didThresholdViolate(ThresholdIntf::Bound bound, double thresholdValue,
                         double value)
{
    switch (bound)
    {
        case ThresholdIntf::Bound::Lower:
        {
            return (value < thresholdValue);
        }
        case ThresholdIntf::Bound::Upper:
        {
            return (value > thresholdValue);
        }
        default:
        {
            error("Invalid threshold bound {BOUND}", "BOUND", bound);
            return false;
        }
    }
}

template <typename errorObj>
auto logAssertThresholdHelper(const std::string& metric, double currentValue,
                              double thresholdValue, ThresholdIntf::Type type,
                              ThresholdIntf::Bound bound)
    -> sdbusplus::message::object_path
{
    return lg2::commit(
        errorObj("METRIC_NAME", metric, "READING_VALUE", currentValue,
                 "THRESHOLD_VALUE", thresholdValue, "TRIGGER_NAME",
                 to_string(type) + "_" + to_string(bound), "UNITS",
                 MetricUnit::Percent));
}

void HealthMetric::logAssertThresholds(double currentValue, Type type,
                                       Bound bound)
{
    namespace errors =
        sdbusplus::error::xyz::openbmc_project::metric::Threshold;
    static const std::map<std::tuple<Type, Bound>,
                          std::function<sdbusplus::message::object_path(
                              const std::string&, double, double, Type, Bound)>>
        thresholdLogMap = {
            {{Type::HardShutdown, Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerCritical>},
            {{Type::HardShutdown, Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperCritical>},
            {{Type::SoftShutdown, Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerCritical>},
            {{Type::SoftShutdown, Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperCritical>},
            {{Type::PerformanceLoss, Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerWarning>},
            {{Type::PerformanceLoss, Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperWarning>},
            {{Type::Critical, Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerCritical>},
            {{Type::Critical, Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperCritical>},
            {{Type::Warning, Bound::Lower},
             &logAssertThresholdHelper<
                 errors::TriggerNumericBelowLowerWarning>},
            {{Type::Warning, Bound::Upper},
             &logAssertThresholdHelper<
                 errors::TriggerNumericAboveUpperWarning>}};
    auto metric = config.name;
    auto& thresholdConf = config.thresholds.at({type, bound});
    if (thresholdConf.assertedLog)
    {
        // Technically we should never get here. But handle anyway.
        lg2::error("Ignoring new log with unresolved outstanding entry: {LOG}",
                   "LOG", std::string(*(thresholdConf.assertedLog)));
        return;
    }
    try
    {
        thresholdConf.assertedLog = thresholdLogMap.at({type, bound})(
            metric, currentValue, thresholdConf.value / 100, type, bound);
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

void HealthMetric::logDeassertThresholds(double currentValue, Type type,
                                         Bound bound)
{
    namespace events =
        sdbusplus::event::xyz::openbmc_project::metric::Threshold;

    auto& thresholdConf = config.thresholds.at({type, bound});
    if (thresholdConf.assertedLog)
    {
        try
        {
            lg2::resolve(*thresholdConf.assertedLog);
        }
        catch (std::exception& ec)
        {
            lg2::error("Unable to resolve {LOG} : {ERROR}", "LOG",
                       std::string(*thresholdConf.assertedLog), "ERROR",
                       ec.what());
        }
        error("DEASSERT: {METRIC} {CUR}", "METRIC", config.name, "CUR",
              currentValue);
        thresholdConf.assertedLog.reset();
    }
    auto it = std::find_if(
        config.thresholds.begin(), config.thresholds.end(),
        [](auto& th) { return th.second.assertedLog.has_value(); });
    // Return if there are outstanding asserts.
    if (it != config.thresholds.end())
    {
        return;
    }
    lg2::commit(events::TriggerNumericReadingNormal(
        "METRIC_NAME", config.name, "READING_VALUE", currentValue,
        "TRIGGER_NAME", to_string(type) + "_" + to_string(bound), "UNITS",
        MetricUnit::Percent));
}

void HealthMetric::checkThreshold(Type type, Bound bound, MValue value)
{
    auto threshold = std::make_tuple(type, bound);
    auto thresholds = ThresholdIntf::value();

    if (thresholds.contains(type) && thresholds[type].contains(bound))
    {
        auto tConfig = config.thresholds.at(threshold);
        auto thresholdValue = tConfig.value / 100 * value.total;
        const auto currentRatio = value.current / value.total;
        thresholds[type][bound] = thresholdValue;
        ThresholdIntf::value(thresholds);
        auto assertions = ThresholdIntf::asserted();
        if (didThresholdViolate(bound, thresholdValue, value.current))
        {
            if (!assertions.contains(threshold))
            {
                assertions.insert(threshold);
                ThresholdIntf::asserted(assertions);
                ThresholdIntf::assertionChanged(type, bound, true,
                                                value.current);
                if (tConfig.sel)
                {
                    logAssertThresholds(currentRatio, type, bound);
                }
                if (tConfig.log)
                {
                    error(
                        "ASSERT: Health Metric {METRIC} crossed {TYPE} upper threshold",
                        "METRIC", config.name, "TYPE", type);
                    startUnit(bus, tConfig.target);
                }
            }
            return;
        }
        else if (assertions.contains(threshold))
        {
            assertions.erase(threshold);
            ThresholdIntf::asserted(assertions);
            ThresholdIntf::assertionChanged(type, bound, false, value.current);
            if (config.thresholds.find(threshold)->second.sel)
            {
                logDeassertThresholds(currentRatio, type, bound);
            }
            if (config.thresholds.find(threshold)->second.log)
            {
                info(
                    "DEASSERT: Health Metric {METRIC} is below {TYPE} upper threshold",
                    "METRIC", config.name, "TYPE", type);
            }
        }
    }
}

void HealthMetric::checkThresholds(MValue value)
{
    if (!ThresholdIntf::value().empty())
    {
        for (auto type : {Type::HardShutdown, Type::SoftShutdown,
                          Type::PerformanceLoss, Type::Critical, Type::Warning})
        {
            checkThreshold(type, Bound::Lower, value);
            checkThreshold(type, Bound::Upper, value);
        }
    }
}

auto HealthMetric::shouldNotify(MValue value) -> bool
{
    if (std::isnan(value.current))
    {
        return true;
    }
    auto changed = std::abs(
        (value.current - lastNotifiedValue) / lastNotifiedValue * 100.0);
    if (changed >= config.hysteresis)
    {
        lastNotifiedValue = value.current;
        return true;
    }
    return false;
}

void HealthMetric::update(MValue value)
{
    ValueIntf::value(value.current, !shouldNotify(value));

    // Maintain window size for threshold calculation
    if (history.size() >= config.windowSize)
    {
        history.pop_front();
    }
    history.push_back(value.current);

    if (history.size() < config.windowSize)
    {
        // Wait for the metric to have enough samples to calculate average
        return;
    }

    double average =
        (std::accumulate(history.begin(), history.end(), 0.0)) / history.size();
    value.current = average;
    checkThresholds(value);
}

void HealthMetric::create(const paths_t& bmcPaths)
{
    info("Create Health Metric: {METRIC}", "METRIC", config.name);
    initProperties();

    std::vector<association_t> associations;
    static constexpr auto forwardAssociation = "measuring";
    static constexpr auto reverseAssociation = "measured_by";
    for (const auto& bmcPath : bmcPaths)
    {
        /*
         * This metric is "measuring" the health for the BMC at bmcPath
         * The BMC at bmcPath is "measured_by" this metric.
         */
        associations.push_back(
            {forwardAssociation, reverseAssociation, bmcPath});
    }
    AssociationIntf::associations(associations);
}

} // namespace phosphor::health::metric
