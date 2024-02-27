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

auto HealthMetricCollection::readCPU() -> bool
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
    std::size_t timeData[CPUStatsIndex::maxIndex] = {0};

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

    for (auto& config : configs)
    {
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
                                    decltype(totalTime){0});

        activeTimeDiff = activeTime - preActiveTime[config.subType];
        totalTimeDiff = totalTime - preTotalTime[config.subType];

        /* Store current active and total time for next calculation */
        preActiveTime[config.subType] = activeTime;
        preTotalTime[config.subType] = totalTime;

        activePercValue = (100.0 * activeTimeDiff) / totalTimeDiff;
        debug("CPU Metric {SUBTYPE}: {VALUE}", "SUBTYPE", config.subType,
              "VALUE", (double)activePercValue);
        /* For CPU, both user and monitor uses percentage values */
        metrics[config.name]->update(MValue(activePercValue, 100));
    }
    return true;
}

auto HealthMetricCollection::readMemory() -> bool
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
            memoryValues[MetricIntf::SubType::memoryShared] += value;
        }
    }

    for (auto& config : configs)
    {
        // Convert kB to Bytes
        auto value = memoryValues.at(config.subType) * 1024;
        auto total = memoryValues.at(MetricIntf::SubType::memoryTotal) * 1024;
        debug("Memory Metric {SUBTYPE}: {VALUE}, {TOTAL}", "SUBTYPE",
              config.subType, "VALUE", value, "TOTAL", total);
        metrics[config.name]->update(MValue(value, total));
    }
    return true;
}

auto HealthMetricCollection::readStorage() -> bool
{
    for (auto& config : configs)
    {
        struct statvfs buffer;
        if (statvfs(config.path.c_str(), &buffer) != 0)
        {
            auto e = errno;
            error("Error from statvfs: {ERROR}, path: {PATH}", "ERROR",
                  strerror(e), "PATH", config.path);
            continue;
        }
        double value = buffer.f_bfree * buffer.f_frsize;
        double total = buffer.f_blocks * buffer.f_frsize;
        debug("Storage Metric {SUBTYPE}: {VALUE}, {TOTAL}", "SUBTYPE",
              config.subType, "VALUE", value, "TOTAL", total);
        metrics[config.name]->update(MValue(value, total));
    }
    return true;
}

void HealthMetricCollection::read()
{
    switch (type)
    {
        case MetricIntf::Type::cpu:
        {
            if (!readCPU())
            {
                error("Failed to read CPU health metric");
            }
            break;
        }
        case MetricIntf::Type::memory:
        {
            if (!readMemory())
            {
                error("Failed to read memory health metric");
            }
            break;
        }
        case MetricIntf::Type::storage:
        {
            if (!readStorage())
            {
                error("Failed to read storage health metric");
            }
            break;
        }
        default:
        {
            error("Unknown health metric type {TYPE}", "TYPE", type);
            break;
        }
    }
}

void HealthMetricCollection::create(const MetricIntf::paths_t& bmcPaths)
{
    metrics.clear();

    for (auto& config : configs)
    {
        metrics[config.name] = std::make_unique<MetricIntf::HealthMetric>(
            bus, type, config, bmcPaths);
    }
}

} // namespace phosphor::health::metric::collection
