#include "health_metric_collection.hpp"

#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <numeric>
#include <unordered_map>

extern "C"
{
#include <sys/statvfs.h>
}

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric::collection
{

static auto readCPU(const configs_t& configs, cache_t& cache) -> bool
{
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
        maxIndex
    };
    constexpr auto procStat = "/proc/stat";
    std::ifstream fileStat(procStat);
    if (!fileStat.is_open())
    {
        error("Unable to open {PATH} for reading CPU stats", "PATH", procStat);
        return false;
    }

    std::string firstLine, labelName;
    std::size_t timeData[CPUStatsIndex::maxIndex];

    std::getline(fileStat, firstLine);
    std::stringstream ss(firstLine);
    ss >> labelName;

    if (labelName.compare("cpu"))
    {
        error("CPU data not available");
        return false;
    }

    for (auto idx = 0; idx < CPUStatsIndex::maxIndex; idx++)
    {
        if (!(ss >> timeData[idx]))
        {
            error("CPU data not correct");
            return false;
        }
    }

    static std::unordered_map<MetricIntf::SubType, uint64_t> preActiveTime,
        preTotalTime;

    for (auto& config : configs)
    {
        // These are actually Jiffies. On the BMC, 1 jiffy usually corresponds
        // to 0.01 second.
        uint64_t activeTime = 0, activeTimeDiff = 0, totalTime = 0,
                 totalTimeDiff = 0;
        double activePercValue = 0;

        if (config.subType == MetricIntf::SubType::cpuTotal)
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
        else if (config.subType == MetricIntf::SubType::cpuKernel)
        {
            activeTime = timeData[CPUStatsIndex::systemIndex];
        }
        else if (config.subType == MetricIntf::SubType::cpuUser)
        {
            activeTime = timeData[CPUStatsIndex::userIndex];
        }

        totalTime = std::accumulate(std::begin(timeData), std::end(timeData),
                                    0);

        activeTimeDiff = activeTime - preActiveTime[config.subType];
        totalTimeDiff = totalTime - preTotalTime[config.subType];

        /* Store current idle and active time for next calculation */
        preActiveTime[config.subType] = activeTime;
        preTotalTime[config.subType] = totalTime;

        activePercValue = (100.0 * activeTimeDiff) / totalTimeDiff;
        debug("CPU Metric {MSTYPE}: {MVALUE}", "MSTYPE",
              std::to_underlying(config.subType), "MVALUE",
              (double)activePercValue);

        cache[config.subType] = std::make_tuple(activePercValue,
                                                activePercValue);
    }

    return true;
}

static auto readMemory(const configs_t& configs, cache_t& cache) -> bool
{
    constexpr auto procMeminfo = "/proc/meminfo";
    std::ifstream memInfo(procMeminfo);
    if (!memInfo.is_open())
    {
        error("Unable to open {PATH} for reading Memory stats", "PATH",
              procMeminfo);
        return false;
    }
    std::string line;
    std::unordered_map<MetricIntf::SubType, double> memoryValues;

    while (std::getline(memInfo, line))
    {
        std::string name;
        double value;
        std::istringstream iss(line);

        if (!(iss >> name >> value))
        {
            continue;
        }
        if (name.starts_with("MemAvailable"))
        {
            memoryValues[MetricIntf::SubType::memoryAvailable] = value;
        }
        else if (name.starts_with("MemFree"))
        {
            memoryValues[MetricIntf::SubType::memoryFree] = value;
        }
        else if (name.starts_with("Buffers") || name.starts_with("Cached"))
        {
            memoryValues[MetricIntf::SubType::memoryBufferedAndCached] += value;
        }
        else if (name.starts_with("MemTotal"))
        {
            memoryValues[MetricIntf::SubType::memoryTotal] = value;
        }
        else if (name.starts_with("Shmem"))
        {
            memoryValues[MetricIntf::SubType::memoryShared] = value;
        }
    }

    for (auto& config : configs)
    {
        double absoluteValue =
            (memoryValues.find(config.subType) != memoryValues.end()
                 ? memoryValues[config.subType]
                 : std::numeric_limits<double>::quiet_NaN());
        auto memoryTotal = memoryValues.find(MetricIntf::SubType::memoryTotal);
        double percentValue =
            (((absoluteValue == std::numeric_limits<double>::quiet_NaN()) ||
              (memoryTotal == memoryValues.end()))
                 ? std::numeric_limits<double>::quiet_NaN()
                 : (memoryTotal->second - absoluteValue) / memoryTotal->second *
                       100);
        cache[config.subType] = std::make_tuple(absoluteValue * 1000,
                                                percentValue);
        info("Memory Metric {MSTYPE}: {ABS_VALUE}, {PERC_VALUE}", "MSTYPE",
             std::to_underlying(config.subType), "ABS_VALUE",
             std::get<0>(cache[config.subType]), "PERC_VALUE",
             std::get<1>(cache[config.subType]));
    }

    return true;
}

static auto readStorage(const configs_t& configs, cache_t& cache) -> bool
{
    for (auto& config : configs)
    {
        struct statvfs buffer;
        if (statvfs(config.path.c_str(), &buffer) != 0)
        {
            auto e = errno;
            error("Error from statvfs: {ERROR}, path: {PATH}", "ERROR",
                  strerror(e), "PATH", config.path);
            cache[config.subType] =
                std::make_tuple(std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        double total = buffer.f_blocks * (buffer.f_frsize / 1024);
        double available = buffer.f_bfree * (buffer.f_frsize / 1024);
        double availablePercent = ((available / total) * 100);
        cache[config.subType] = std::make_tuple(available, availablePercent);

        info("Storage Metric {MSTYPE}: {TOTAL} {AVAIL} {AVAIL_PERCENT}",
             "MSTYPE", std::to_underlying(config.subType), "TOTAL", total,
             "AVAIL", available, "AVAIL_PERCENT", availablePercent);
    }
    return true;
}

const auto handlers =
    std::map<MetricIntf::Type, std::function<bool(const configs_t&, cache_t&)>>{
        {MetricIntf::Type::cpu, readCPU},
        {MetricIntf::Type::memory, readMemory},
        {MetricIntf::Type::storage, readStorage}};

void HealthMetricCollection::read()
{
    // Read the health metrics from the system
    auto handler = handlers.find(type);
    if (handler != handlers.end())
    {
        if (!(*handler).second(configs, cache))
        {
            error("Failed to read health metric {MTYPE}", "MTYPE",
                  std::to_underlying(type));
            return;
        }
    }
    else
    {
        error("No read handler found for {MTYPE}", "MTYPE",
              std::to_underlying(type));
        return;
    }

    for (auto& metric : metrics)
    {
        info("Update health metric {MSTYPE} with value {ABSOLUTE}, {PERCENT}",
             "MSTYPE", std::to_underlying(metric.first), "ABSOLUTE",
             get<0>(cache[metric.first]), "PERCENT",
             get<1>(cache[metric.first]));
        metric.second->update(cache[metric.first]);
    }
}

void HealthMetricCollection::create(const MetricIntf::paths_t& bmcPaths)
{
    // Clear the cache for any existing values
    cache.clear();
    metrics.clear();

    // Read the health metrics from the system
    auto handler = handlers.find(type);
    if (handler != handlers.end())
    {
        if (!(*handler).second(configs, cache))
        {
            error("Failed to read health metric {MTYPE}", "MTYPE",
                  std::to_underlying(type));
            return;
        }
    }
    else
    {
        error("No read metric handler found for {MTYPE}", "MTYPE",
              std::to_underlying(type));
        return;
    }

    for (auto& config : configs)
    {
        if (config.subType == MetricIntf::SubType::NA)
        {
            continue;
        }
        metrics[config.subType] = std::make_unique<MetricIntf::HealthMetric>(
            bus, type, config, bmcPaths);
    }
}

} // namespace phosphor::health::metric::collection
