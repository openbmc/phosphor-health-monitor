#include "health_metric.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cmath>
#include <numeric>
#include <unordered_map>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using association_t = std::tuple<std::string, std::string, std::string>;

static const auto validThresholds =
    std::unordered_map<ThresholdIntf::Type, std::string>{
        {ThresholdIntf::Type::Critical, "Critical"},
        {ThresholdIntf::Type::Warning, "Warning"}};

auto HealthMetric::getPath(SubType subType) -> std::string
{
    std::string path;
    static constexpr auto BMCNode = "/bmc/";
    if (subType == SubType::cpuTotal)
    {
        path = std::string(PathIntf::value) + BMCNode + PathIntf::total_cpu;
    }
    else if (subType == SubType::cpuKernel)
    {
        path = std::string(PathIntf::value) + BMCNode + PathIntf::kernel_cpu;
    }
    else if (subType == SubType::cpuUser)
    {
        path = std::string(PathIntf::value) + BMCNode + PathIntf::user_cpu;
    }
    else if (subType == SubType::memoryAvailable)
    {
        path = std::string(PathIntf::value) + BMCNode +
               PathIntf::available_memory;
    }
    else if (subType == SubType::memoryBufferedAndCached)
    {
        path = std::string(PathIntf::value) + BMCNode +
               PathIntf::buffered_and_cached_memory;
    }
    else if (subType == SubType::memoryFree)
    {
        path = std::string(PathIntf::value) + BMCNode + PathIntf::free_memory;
    }
    else if (subType == SubType::memoryShared)
    {
        path = std::string(PathIntf::value) + BMCNode + PathIntf::shared_memory;
    }
    else if (subType == SubType::memoryTotal)
    {
        path = std::string(PathIntf::value) + BMCNode + PathIntf::total_memory;
    }
    else if (subType == SubType::storageReadWrite)
    {
        path = std::string(PathIntf::value) + BMCNode +
               PathIntf::read_write_storage;
    }
    else
    {
        error("Invalid Memory mertic {MSTYPE}", "MSTYPE",
              std::to_underlying(subType));
        return "";
    }
    info("Health Metric Object path: {PATH}", "PATH", path);
    return path;
}

void HealthMetric::setProperties()
{
    switch (config.subType)
    {
        case SubType::cpuTotal:
        case SubType::cpuKernel:
        case SubType::cpuUser:
        {
            ValueIntf::unit(ValueIntf::Unit::Percent);
            ValueIntf::minValue(0.0);
            ValueIntf::maxValue(100.0);
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
            ValueIntf::unit(ValueIntf::Unit::Bytes);
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
    auto thresholdStr = validThresholds.find(type)->second;

    if (ThresholdIntf::value().contains(type) &&
        ThresholdIntf::value().at(type).contains(bound))
    {
        auto thresholdValue = ThresholdIntf::value().at(type).at(bound);
        if (std::isfinite(thresholdValue) && value > thresholdValue)
        {
            if (!ThresholdIntf::asserted().contains(threshold))
            {
                auto asserted = ThresholdIntf::asserted();
                asserted.insert(threshold);
                ThresholdIntf::asserted(asserted);
                ThresholdIntf::assertionChanged(type, bound, true, value);
                if (config.thresholds.find(threshold)->second.log)
                {
                    error(
                        "ASSERT: Health Metric {MNAME} crossed {TYPE} upper threshold",
                        "METRIC", config.name, "TYPE", thresholdStr);
                    startUnit(bus,
                              config.thresholds.find(threshold)->second.target);
                }
            }
            return;
        }
        else if (ThresholdIntf::asserted().contains(threshold))
        {
            auto asserted = ThresholdIntf::asserted();
            asserted.erase(threshold);
            ThresholdIntf::asserted(asserted);
            ThresholdIntf::assertionChanged(type, bound, false, value);
            if (config.thresholds.find(threshold)->second.log)
            {
                info(
                    "DEASSERT: Health Metric {MNAME} is below {TYPE} upper threshold",
                    "METRIC", config.name, "LEVEL", thresholdStr);
            }
        }
    }
}

void HealthMetric::checkThresholds(double value)
{
    if (!ThresholdIntf::value().empty())
    {
        checkThreshold(ThresholdIntf::Type::Critical,
                       ThresholdIntf::Bound::Upper, value);
        checkThreshold(ThresholdIntf::Type::Warning,
                       ThresholdIntf::Bound::Upper, value);
    }
}

void HealthMetric::update(value_t value)
{
    // Maintain window size for metric
    if (history.size() >= config.windowSize)
    {
        history.pop_front();
    }
    history.push_back(get<0>(value));

    if (history.size() < config.windowSize)
    {
        // Wait for the metric to have enough samples to calculate average
        warning("Not enough samples to calculate average");
        return;
    }

    double average = (std::accumulate(history.begin(), history.end(), 0.0)) /
                     history.size();
    ValueIntf::value(average);
    checkThresholds(get<1>(value));
}

void HealthMetric::create(const paths_t& bmcPaths)
{
    info("Create Health Metric: {MNAME}", "MNAME", config.name);
    setProperties();

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
