#include "health_metric.hpp"

#include <phosphor-logging/lg2.hpp>

#include <numeric>
#include <unordered_map>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using association_t = std::tuple<std::string, std::string, std::string>;

auto HealthMetric::getPath(phosphor::health::metric::Type type,
                           std::string name, SubType subType) -> std::string
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
            if (type == phosphor::health::metric::Type::storage)
            {
                static constexpr auto nameDelimiter = "_";
                auto storageType = name.substr(
                    name.find_last_of(nameDelimiter) + 1, name.length());
                std::ranges::for_each(storageType,
                                      [](auto& c) { c = std::tolower(c); });
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

    using bound_map_t = std::map<ThresholdIntf::Bound, double>;
    std::map<ThresholdIntf::Type, bound_map_t> thresholds;
    for (const auto& [key, value] : config.thresholds)
    {
        auto type = std::get<ThresholdIntf::Type>(key);
        auto bound = std::get<ThresholdIntf::Bound>(key);
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

void HealthMetric::checkThreshold(ThresholdIntf::Type type,
                                  ThresholdIntf::Bound bound, MValue value)
{
    auto threshold = std::make_tuple(type, bound);
    auto thresholds = ThresholdIntf::value();

    if (thresholds.contains(type) && thresholds[type].contains(bound))
    {
        auto tConfig = config.thresholds.at(threshold);
        auto thresholdValue = tConfig.value / 100 * value.total;
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
        for (auto type :
             {ThresholdIntf::Type::HardShutdown,
              ThresholdIntf::Type::SoftShutdown,
              ThresholdIntf::Type::PerformanceLoss,
              ThresholdIntf::Type::Critical, ThresholdIntf::Type::Warning})
        {
            checkThreshold(type, ThresholdIntf::Bound::Lower, value);
            checkThreshold(type, ThresholdIntf::Bound::Upper, value);
        }
    }
}

void HealthMetric::update(MValue value)
{
    ValueIntf::value(value.current);

    // Maintain window size for threshold calculation
    if (history.size() >= config.windowSize)
    {
        history.pop_front();
    }
    history.push_back(value.current);

    if (history.size() < config.windowSize)
    {
        // Wait for the metric to have enough samples to calculate average
        debug("Not enough samples to calculate average");
        return;
    }

    double average = (std::accumulate(history.begin(), history.end(), 0.0)) /
                     history.size();
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
