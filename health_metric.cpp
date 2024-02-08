#include "health_metric.hpp"

#include <phosphor-logging/lg2.hpp>

#include <numeric>
#include <unordered_map>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using association_t = std::tuple<std::string, std::string, std::string>;

auto HealthMetric::getPath(SubType subType) -> std::string
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
        case SubType::storageReadWrite:
        {
            return std::string(BmcPath) + "/" + PathIntf::read_write_storage;
        }
        default:
        {
            error("Invalid Memory metric {TYPE}", "TYPE",
                  std::to_underlying(subType));
            return "";
        }
    }
}

void HealthMetric::initProperties()
{
    switch (config.subType)
    {
        case SubType::cpuTotal:
        case SubType::cpuKernel:
        case SubType::cpuUser:
        {
            ValueIntf::unit(ValueIntf::Unit::Percent, true);
            ValueIntf::minValue(0.0, true);
            ValueIntf::maxValue(100.0, true);
            break;
        }
        case SubType::memoryAvailable:
        case SubType::memoryBufferedAndCached:
        case SubType::memoryFree:
        case SubType::memoryShared:
        case SubType::memoryTotal:
        case SubType::storageReadWrite:
        default:
        {
            ValueIntf::unit(ValueIntf::Unit::Bytes, true);
            ValueIntf::minValue(0.0, true);
        }
    }
    ValueIntf::value(std::numeric_limits<double>::quiet_NaN());

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
            bounds.emplace(bound, value.value);
            thresholds.emplace(type, bounds);
        }
        else
        {
            threshold->second.emplace(bound, value.value);
        }
    }
    ThresholdIntf::value(thresholds);
}

void HealthMetric::checkThreshold(ThresholdIntf::Type type,
                                  ThresholdIntf::Bound bound, double value)
{
    auto threshold = std::make_tuple(type, bound);
    auto thresholds = ThresholdIntf::value();

    if (thresholds.contains(type) && thresholds[type].contains(bound))
    {
        auto thresholdValue = thresholds[type][bound];
        auto assertions = ThresholdIntf::asserted();
        if (value > thresholdValue)
        {
            if (!assertions.contains(threshold))
            {
                assertions.insert(threshold);
                ThresholdIntf::asserted(assertions);
                ThresholdIntf::assertionChanged(type, bound, true, value);
                auto tConfig = config.thresholds.at(threshold);
                if (tConfig.log)
                {
                    error(
                        "ASSERT: Health Metric {METRIC} crossed {TYPE} upper threshold",
                        "METRIC", config.name, "TYPE",
                        sdbusplus::message::convert_to_string(type));
                    startUnit(bus, tConfig.target);
                }
            }
            return;
        }
        else if (assertions.contains(threshold))
        {
            assertions.erase(threshold);
            ThresholdIntf::asserted(assertions);
            ThresholdIntf::assertionChanged(type, bound, false, value);
            if (config.thresholds.find(threshold)->second.log)
            {
                info(
                    "DEASSERT: Health Metric {METRIC} is below {TYPE} upper threshold",
                    "METRIC", config.name, "TYPE",
                    sdbusplus::message::convert_to_string(type));
            }
        }
    }
}

void HealthMetric::checkThresholds(double value)
{
    if (!ThresholdIntf::value().empty())
    {
        for (auto type :
             {ThresholdIntf::Type::HardShutdown,
              ThresholdIntf::Type::SoftShutdown,
              ThresholdIntf::Type::PerformanceLoss,
              ThresholdIntf::Type::Critical, ThresholdIntf::Type::Warning})
        {
            checkThreshold(type, ThresholdIntf::Bound::Upper, value);
        }
    }
}

void HealthMetric::update(MValue value)
{
    // Maintain window size for metric
    if (history.size() >= config.windowSize)
    {
        history.pop_front();
    }
    history.push_back(value.user);

    if (history.size() < config.windowSize)
    {
        // Wait for the metric to have enough samples to calculate average
        info("Not enough samples to calculate average");
        return;
    }

    double average = (std::accumulate(history.begin(), history.end(), 0.0)) /
                     history.size();
    ValueIntf::value(average);
    checkThresholds(value.monitor);
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
