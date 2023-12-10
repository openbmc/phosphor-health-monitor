#include "health_metric.hpp"

#include "health_utils.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cmath>
#include <fstream>
#include <numeric>

PHOSPHOR_LOG2_USING;

constexpr auto metricPercentageMinValue = 0;
constexpr auto metricPercentageMaxValue = 100;

constexpr auto BMCCPUTotalObjectPath =
    "/xyz/openbmc_project/metric/bmc/cpu/total";

static constexpr auto BMCNode = "/bmc/";
constexpr auto metricBMCForwardAssociation = "measuring";
constexpr auto BMCMetricReverseAssociation = "measured_by";

namespace phosphor
{ // namespace phosphor
namespace health
{ // namespace health
namespace metric
{ // namespace metric

using AssociationTuple = std::tuple<std::string, std::string, std::string>;
using namespace phosphor::health::utils;

static const std::map<ThresholdInterface::Type, std::string>
    validThresholdTypes = {
        {ThresholdInterface::Type::Critical, thresholdCritical},
        {ThresholdInterface::Type::Warning, thresholdWarning}};

std::string HealthMetric::getObjectPath(MetricSubtype metricSubtype)
{
    if (metricSubtype == MetricSubtype::CPUTotal)
    {
        return std::string(PathInterface::value) + BMCNode + "total_cpu";
    }
    else if (metricSubtype == MetricSubtype::CPUKernel)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::kernel_cpu;
    }
    else if (metricSubtype == MetricSubtype::CPUUser)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::user_cpu;
    }
    else if (metricSubtype == MetricSubtype::memoryAvailable)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::available_memory;
    }
    else if (metricSubtype == MetricSubtype::memoryBufferedAndCached)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::buffered_and_cached_memory;
    }
    else if (metricSubtype == MetricSubtype::memoryFree)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::free_memory;
    }
    else if (metricSubtype == MetricSubtype::memoryShared)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::shared_memory;
    }
    else if (metricSubtype == MetricSubtype::memoryTotal)
    {
        return std::string(PathInterface::value) + BMCNode +
               PathInterface::total_memory;
    }
    else
    {
        error("Invalid Memory mertic {METRIC_SUBTYPE}", "METRIC_SUBTYPE",
              (int)metricSubtype);
        return "";
    }
}

void HealthMetric::setHealthMetricProperties()
{
    switch (metricConfig.metricSubtype)
    {
        case MetricSubtype::CPUTotal:
        case MetricSubtype::CPUKernel:
        case MetricSubtype::CPUUser:
        {
            ValueInterface::unit(ValueInterface::Unit::Percent);
            ValueInterface::minValue(metricPercentageMinValue);
            ValueInterface::maxValue(metricPercentageMaxValue);
            break;
        }
        case MetricSubtype::memoryAvailable:
        case MetricSubtype::memoryBufferedAndCached:
        case MetricSubtype::memoryFree:
        case MetricSubtype::memoryShared:
        case MetricSubtype::memoryTotal:
        default:
        {
            ValueInterface::unit(ValueInterface::Unit::Bytes);
        }
    }
    ValueInterface::value(std::numeric_limits<double>::quiet_NaN());

    std::map<std::tuple<ThresholdInterface::Type, ThresholdInterface::Bound>,
             double>
        thresholds;
    for (const auto& [thresholdKey, thresholdConfig] :
         metricConfig.thresholdConfigs)
    {
        thresholds.emplace(thresholdKey, thresholdConfig.value);
    }
    ThresholdInterface::value(thresholds);
}

void HealthMetric::checkHealthMetricThreshold(ThresholdInterface::Type type,
                                              ThresholdInterface::Bound bound,
                                              double value)
{
    auto threshold = std::make_tuple(type, bound);
    auto thresholdStr = validThresholdTypes.find(type)->second;

    if (ThresholdInterface::value().contains(threshold))
    {
        auto thresholdValue = ThresholdInterface::value().at(threshold);
        if (std::isfinite(thresholdValue) && value > thresholdValue)
        {
            if (!ThresholdInterface::asserted().contains(threshold))
            {
                auto assertedThresholds = ThresholdInterface::asserted();
                assertedThresholds.insert(threshold);
                ThresholdInterface::asserted(assertedThresholds);
                ThresholdInterface::assertionChanged(type, bound, true, value);
                if (metricConfig.thresholdConfigs.find(threshold)
                        ->second.logMessage)
                {
                    error(
                        "ASSERT: Health Metric {METRIC} crossed {LEVEL} high threshold",
                        "METRIC", metricConfig.metricName, "LEVEL",
                        thresholdStr);
                    startUnit(bus,
                              metricConfig.thresholdConfigs.find(threshold)
                                  ->second.target);
                }
            }
            return;
        }
        else if (ThresholdInterface::asserted().contains(threshold))
        {
            auto assertedThresholds = ThresholdInterface::asserted();
            assertedThresholds.erase(threshold);
            ThresholdInterface::asserted(assertedThresholds);
            ThresholdInterface::assertionChanged(type, bound, false, value);
            if (metricConfig.thresholdConfigs.find(threshold)
                    ->second.logMessage)
            {
                info(
                    "DEASSERT: Health Metric {METRIC} is below {LEVEL} high threshold",
                    "METRIC", metricConfig.metricName, "LEVEL", thresholdStr);
            }
        }
    }
}

void HealthMetric::checkHealthMetricThresholds(double value)
{
    if (!ThresholdInterface::value().empty())
    {
        checkHealthMetricThreshold(ThresholdInterface::Type::Critical,
                                   ThresholdInterface::Bound::Upper, value);
        checkHealthMetricThreshold(ThresholdInterface::Type::Warning,
                                   ThresholdInterface::Bound::Upper, value);
    }
}

void HealthMetric::updateHealthMetric(double value)
{
    // Maintain window size for metric
    if (metricValueHistory.size() >= metricConfig.windowSize)
    {
        metricValueHistory.pop_front();
    }
    metricValueHistory.push_back(value);

    if (metricValueHistory.size() < metricConfig.windowSize)
    {
        // Wait for the metric to have enough samples to calculate average
        return;
    }

    double metricValueSum = std::accumulate(metricValueHistory.begin(),
                                            metricValueHistory.end(), 0.0);
    double metricValueAverage = metricValueSum / metricValueHistory.size();

    ValueInterface::value(metricValueAverage);
    checkHealthMetricThresholds(value);
}

void HealthMetric::createHealthMetric(
    const std::vector<std::string>& bmcInventoryPaths)
{
    info("Create Health Metric: {METRIC}", "METRIC", metricConfig.metricName);

    setHealthMetricProperties();

    std::vector<AssociationTuple> associationTuples;
    for (const auto& bmcInventoryPath : bmcInventoryPaths)
    {
        // This metric is "measuring" the health for the BMC at bmcInventoryPath
        // The BMC at bmcInventoryPath is "measured_by" this metric.
        associationTuples.push_back({metricBMCForwardAssociation,
                                     BMCMetricReverseAssociation,
                                     bmcInventoryPath});
    }
    AssociationInterface::associations(associationTuples);
}

} // namespace metric
} // namespace health
} // namespace phosphor
