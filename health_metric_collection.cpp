#include "health_metric_collection.hpp"

#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <numeric>
#include <unordered_map>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric::collection
{

constexpr auto procStat = "/proc/stat";
constexpr auto procMeminfo = "/proc/meminfo";

constexpr auto metricCPU = "cpu";
constexpr auto memoryTotal = "MemTotal";
constexpr auto memoryFree = "MemFree";
constexpr auto memoryAvailable = "MemAvailable";
constexpr auto memoryShared = "Shmem";
constexpr auto memoryBuffers = "Buffers";
constexpr auto memoryCached = "Cached";

constexpr auto BMCInventoryItem = "xyz.openbmc_project.Inventory.Item.Bmc";

constexpr auto BytesMultiplier = 1000;

enum CPUStatsIndex
{
    userIndex = 0,
    niceIndex,
    systemIndex,
    idleIndex,
    iowaitIndex,
    irqIndex,
    softirqIndex,
    stealIndex,
    guestUserIndex,
    guestNiceIndex,
    maxCPUStatsIndex
};

static bool
    readCPUMetric(const std::vector<HealthMetricConfig>& healthMetricConfigs,
                  HealthMetricCache& healthMetricCache)
{
    std::ifstream fileStat(procStat);
    if (!fileStat.is_open())
    {
        error("Unable to open {PATH} for reading CPU stats", "PATH", procStat);
        return -1;
    }

    std::string firstLine, labelName;
    std::size_t timeData[CPUStatsIndex::maxCPUStatsIndex];

    std::getline(fileStat, firstLine);
    std::stringstream ss(firstLine);
    ss >> labelName;

    debug("CPU Stats Label Name is {LABEL_NAME}", "LABEL_NAME", labelName);

    if (labelName.compare(metricCPU))
    {
        error("CPU data not available");
        return false;
    }

    for (auto idx = 0; idx < CPUStatsIndex::maxCPUStatsIndex; idx++)
    {
        if (!(ss >> timeData[idx]))
        {
            error("CPU data not correct");
            return false;
        }
    }

    static std::unordered_map<MetricSubtype, uint64_t> preActiveTime,
        preTotalTime;

    for (auto& metricConfig : healthMetricConfigs)
    {
        // These are actually Jiffies. On the BMC, 1 jiffy usually corresponds
        // to 0.01 second.
        uint64_t activeTime = 0, activeTimeDiff = 0, totalTime = 0,
                 totalTimeDiff = 0;
        double activePercValue = 0;

        if (metricConfig.metricSubtype == MetricSubtype::CPUTotal)
        {
            activeTime = timeData[CPUStatsIndex::userIndex] +
                         timeData[CPUStatsIndex::niceIndex] +
                         timeData[CPUStatsIndex::systemIndex] +
                         timeData[CPUStatsIndex::irqIndex] +
                         timeData[CPUStatsIndex::softirqIndex] +
                         timeData[CPUStatsIndex::stealIndex] +
                         timeData[CPUStatsIndex::guestUserIndex] +
                         timeData[CPUStatsIndex::guestNiceIndex];
        }
        else if (metricConfig.metricSubtype == MetricSubtype::CPUKernel)
        {
            activeTime = timeData[CPUStatsIndex::systemIndex];
        }
        else if (metricConfig.metricSubtype == MetricSubtype::CPUUser)
        {
            activeTime = timeData[CPUStatsIndex::userIndex];
        }

        totalTime = std::accumulate(std::begin(timeData), std::end(timeData),
                                    0);

        activeTimeDiff = activeTime - preActiveTime[metricConfig.metricSubtype];
        totalTimeDiff = totalTime - preTotalTime[metricConfig.metricSubtype];

        /* Store current idle and active time for next calculation */
        preActiveTime[metricConfig.metricSubtype] = activeTime;
        preTotalTime[metricConfig.metricSubtype] = totalTime;

        activePercValue = (100.0 * activeTimeDiff) / totalTimeDiff;
        debug("CPU Metric {METRIC_SUBTYPE}: {METRIC_VALUE}", "METRIC_SUBTYPE",
              std::to_underlying(metricConfig.metricSubtype), "METRIC_VALUE",
              (double)activePercValue);

        healthMetricCache[metricConfig.metricSubtype] =
            std::make_tuple(activePercValue, activePercValue);
    }

    return true;
}

static bool
    readMemoryMetric(const std::vector<HealthMetricConfig>& healthMetricConfigs,
                     HealthMetricCache& healthMetricCache)
{
    std::ifstream memInfo(procMeminfo);
    if (!memInfo.is_open())
    {
        error("Unable to open {PATH} for reading Memory stats", "PATH",
              procMeminfo);
        return false;
    }
    std::string line;
    std::unordered_map<MetricSubtype, double> memoryValues;

    while (std::getline(memInfo, line))
    {
        std::string name;
        double value;
        std::istringstream iss(line);

        if (!(iss >> name >> value))
        {
            continue;
        }
        if (name.starts_with(memoryAvailable))
        {
            memoryValues[MetricSubtype::memoryAvailable] = value;
        }
        else if (name.starts_with(memoryFree))
        {
            memoryValues[MetricSubtype::memoryFree] = value;
        }
        else if (name.starts_with(memoryBuffers) ||
                 name.starts_with(memoryCached))
        {
            memoryValues[MetricSubtype::memoryBufferedAndCached] += value;
        }
        else if (name.starts_with(memoryTotal))
        {
            memoryValues[MetricSubtype::memoryTotal] = value;
        }
        else if (name.starts_with(memoryShared))
        {
            memoryValues[MetricSubtype::memoryShared] = value;
        }
    }

    for (auto& metricConfig : healthMetricConfigs)
    {
        double metricAbsoluteValue =
            (memoryValues.find(metricConfig.metricSubtype) != memoryValues.end()
                 ? memoryValues[metricConfig.metricSubtype]
                 : std::numeric_limits<double>::quiet_NaN());
        auto memoryTotal = memoryValues.find(MetricSubtype::memoryTotal);
        double metricPercentageValue =
            (((metricAbsoluteValue ==
               std::numeric_limits<double>::quiet_NaN()) ||
              (memoryTotal == memoryValues.end()))
                 ? std::numeric_limits<double>::quiet_NaN()
                 : (memoryTotal->second - metricAbsoluteValue) /
                       memoryTotal->second * 100);
        healthMetricCache[metricConfig.metricSubtype] = std::make_tuple(
            metricAbsoluteValue * BytesMultiplier, metricPercentageValue);
        info(
            "Memory Metric {METRIC_SUBTYPE}: {METRIC_ABSOLUTE_VALUE}, {METRIC_PERCENTAGE_VALUE}",
            "METRIC_SUBTYPE", std::to_underlying(metricConfig.metricSubtype),
            "METRIC_ABSOLUTE_VALUE",
            std::get<0>(healthMetricCache[metricConfig.metricSubtype]),
            "METRIC_PERCENTAGE_VALUE",
            std::get<1>(healthMetricCache[metricConfig.metricSubtype]));
    }

    return true;
}

static bool readStorageMetric(
    const std::vector<HealthMetricConfig>& healthMetricConfigs,
    HealthMetricCache& healthMetricCache)
{
    return false;
}

const std::map<MetricType,
               std::function<bool(const std::vector<HealthMetricConfig>&,
                                  HealthMetricCache&)>>
    metricMap = {
        {MetricType::CPU, readCPUMetric},
        {MetricType::memory, readMemoryMetric},
        {MetricType::storage, readStorageMetric},
};

void HealthMetricCollection::readHealthMetricCollection()
{
    // Read the health metrics from the system
    auto metricHandler = metricMap.find(metricType);
    if (metricHandler != metricMap.end())
    {
        if (!(*metricHandler).second(healthMetricConfigs, healthMetricCache))
        {
            error("Failed to read health metric {METRIC_TYPE}", "METRIC_TYPE",
                  std::to_underlying(metricType));
            return;
        }
    }
    else
    {
        error("No read metric handler found for {METRIC_TYPE}", "METRIC_TYPE",
              std::to_underlying(metricType));
        return;
    }

    for (auto& metric : healthMetrics)
    {
        info(
            "Update health metric {METRIC_SUBTYPE} with value {METRIC_ABSOLUTE_VALUE}, {METRIC_PERCENTAGE_VALUE}",
            "METRIC_SUBTYPE", std::to_underlying(metric.first),
            "METRIC_ABSOLUTE_VALUE", get<0>(healthMetricCache[metric.first]),
            "METRIC_PERCENTAGE_VALUE", get<1>(healthMetricCache[metric.first]));
        metric.second->updateHealthMetric(healthMetricCache[metric.first]);
    }
}

void HealthMetricCollection::createHealthMetricCollection(
    const std::vector<std::string>& bmcInventoryPaths)
{
    // Clear the cache for any existing values
    healthMetricCache.clear();
    healthMetrics.clear();

    // Read the health metrics from the system
    auto metricHandler = metricMap.find(metricType);
    if (metricHandler != metricMap.end())
    {
        if (!(*metricHandler).second(healthMetricConfigs, healthMetricCache))
        {
            error("Failed to read health metric {METRIC_TYPE}", "METRIC_TYPE",
                  std::to_underlying(metricType));
            return;
        }
    }
    else
    {
        error("No read metric handler found for {METRIC_TYPE}", "METRIC_TYPE",
              std::to_underlying(metricType));
        return;
    }

    for (auto& metricConfig : healthMetricConfigs)
    {
        healthMetrics[metricConfig.metricSubtype] =
            std::make_unique<HealthMetric>(bus, metricType, metricConfig,
                                           bmcInventoryPaths);
    }

    // TODO: If needed start the monitoring for this collection
}

} // namespace phosphor::health::metric::collection
