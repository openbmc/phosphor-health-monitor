#pragma once

#include <xyz/openbmc_project/Common/Threshold/server.hpp>

#include <limits>
#include <map>
#include <string>
#include <vector>

namespace phosphor
{
namespace health
{
namespace metric
{
namespace config
{

using ThresholdInterface =
    sdbusplus::xyz::openbmc_project::Common::server::Threshold;

// Default metric attribute values
constexpr auto defaultFrequency = 1;
constexpr auto defaultWindowSize = 1;
constexpr auto defaultHighThresholdValue = 100.0;
constexpr auto defaultCriticalThresholdLog = false;
constexpr auto defaultWarningThresholdLog = false;
constexpr auto defaultThresholdTarget = "";
constexpr auto defaultPath = "";

constexpr auto cpuMetricName = "CPU";

constexpr auto thresholdTypeKeyIndex = 0;
constexpr auto thresholdBoundKeyIndex = 1;

constexpr auto thresholdCritical = "Critical";
constexpr auto thresholdWarning = "Warning";

enum class MetricType
{
    CPU = 0,
    memory,
    storage,
    inode,
    unknown
};

enum class MetricSubtype
{
    // CPU subtypes
    CPUKernel = 0,
    CPUTotal,
    CPUUser,
    // Memory subtypes
    memoryAvailable,
    memoryBufferedAndCached,
    memoryFree,
    memoryShared,
    memoryTotal,
    NA
};

struct ThresholdConfig
{
    double value = std::numeric_limits<double>::quiet_NaN();
    bool logMessage;
    std::string target;
};

class HealthMetricConfig
{
  public:
    virtual ~HealthMetricConfig() = default;

    /** @brief The name of the metric. */
    std::string metricName;
    /** @brief The metric subtype. */
    MetricSubtype metricSubtype;
    /** @brief The collection frequency for the metric. */
    size_t collectionFrequency;
    /** @brief The window size for the metric. */
    size_t windowSize;
    /** @brief The threshold configs for the metric. */
    std::map<std::tuple<ThresholdInterface::Type, ThresholdInterface::Bound>,
             ThresholdConfig>
        thresholdConfigs;
    /** @brief The path for filesystem metric */
    std::string path;
};

/** @brief Get the health metric configs. */
std::map<MetricType, std::vector<HealthMetricConfig>> getHealthMetricConfigs();

} // namespace config
} // namespace metric
} // namespace health
} // namespace phosphor
