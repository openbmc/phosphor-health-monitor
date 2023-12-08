#include "config.h"

#include "health_metric_config.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <iostream>

PHOSPHOR_LOG2_USING;

// Metric Names
constexpr auto CPUMetric = "CPU";
constexpr auto memoryMetric = "Memory";
constexpr auto storageMetric = "Storage";
constexpr auto inodeMetric = "Inode";

constexpr auto metricNameDelimiter = "_";

// CPU Metric names
constexpr auto CPUTotal = "CPU_Total";
constexpr auto CPUUser = "CPU_User";
constexpr auto CPUKernel = "CPU_Kernel";

// Memory Metric names
constexpr auto memoryTotal = "Memory_Total";
constexpr auto memoryFree = "Memory_Free";
constexpr auto memoryAvailable = "Memory_Available";
constexpr auto memoryShared = "Memory_Shared";
constexpr auto memoryBufferedAndCached = "Memory_Buffered_And_Cached";

// FS Stats Metric names
constexpr auto storageRW = "Storage_RW";
constexpr auto storageTmp = "Storage_Tmp";

// Metric attributes
constexpr auto collectionFrequency = "Frequency";
constexpr auto windowSize = "Window_size";
constexpr auto threshold = "Threshold";
constexpr auto thresholdValue = "Value";
constexpr auto thresholdLog = "Log";
constexpr auto thresholdTarget = "Target";
constexpr auto filePath = "Path";

namespace phosphor
{ // namespace phosphor
namespace health
{ // namespace health
namespace metric
{ // namespace metric
namespace config
{ // namespace config

using Json = nlohmann::json;
using namespace phosphor::health::metric::config;

constexpr auto defaultThresholdBound = ThresholdInterface::Bound::Upper;

static const std::map<std::string, ThresholdInterface::Type>
    validThresholdTypes = {
        {thresholdCritical, ThresholdInterface::Type::Critical},
        {thresholdWarning, ThresholdInterface::Type::Warning}};

static const std::map<std::string, MetricType> validMetricTypes = {
    {CPUMetric, MetricType::CPU},
    {memoryMetric, MetricType::memory},
    {storageMetric, MetricType::storage},
    {inodeMetric, MetricType::inode}};

static const std::map<std::string, MetricSubtype> validMetricSubtypes = {
    {CPUTotal, MetricSubtype::CPUTotal},
    {CPUUser, MetricSubtype::CPUUser},
    {CPUKernel, MetricSubtype::CPUKernel},
    {memoryTotal, MetricSubtype::memoryTotal},
    {memoryFree, MetricSubtype::memoryFree},
    {memoryAvailable, MetricSubtype::memoryAvailable},
    {memoryShared, MetricSubtype::memoryShared},
    {memoryBufferedAndCached, MetricSubtype::memoryBufferedAndCached}};

// Default health metric config
Json defaultHealthMetricConfig = R"({
    "CPU_Total": {
        "Frequency": 1,
        "Window_size": 120,
        "Threshold": {
            "Critical": {
                "Value": 90.0,
                "Log": true,
                "Target": ""
            },
            "Warning": {
                "Value": 80.0,
                "Log": false,
                "Target": ""
            }
        }
    },
    "CPU_User": {
        "Frequency": 1,
        "Window_size": 120,
        "Threshold": {
            "Critical": {
                "Value": 90.0,
                "Log": true,
                "Target": ""
            },
            "Warning": {
                "Value": 80.0,
                "Log": false,
                "Target": ""
            }
        }
    },
    "CPU_Kernel": {
        "Frequency": 1,
        "Window_size": 120,
        "Threshold": {
            "Critical": {
                "Value": 90.0,
                "Log": true,
                "Target": ""
            },
            "Warning": {
                "Value": 80.0,
                "Log": false,
                "Target": ""
            }
        }
    },
    "Memory_Available": {
        "Frequency": 1,
        "Window_size": 120,
        "Threshold": {
            "Critical": {
                "Value": 85.0,
                "Log": true,
                "Target": ""
            }
        }
    },
    "Storage_RW": {
        "Path": "/run/initramfs/rw",
        "Frequency": 1,
        "Window_size": 120,
        "Threshold": {
            "Critical": {
                "Value": 85.0,
                "Log": true,
                "Target": ""
            }
        }
    },
    "Storage_TMP": {
        "Path": "/tmp",
        "Frequency": 1,
        "Window_size": 120,
        "Threshold": {
            "Critical": {
                "Value": 85.0,
                "Log": true,
                "Target": ""
            }
        }
    }
})"_json;

HealthMetricConfig getHealthMetricConfig(Json& jsonObj)
{
    HealthMetricConfig healthMetricConfig = {};
    healthMetricConfig.collectionFrequency = jsonObj.value(collectionFrequency,
                                                           defaultFrequency);
    healthMetricConfig.windowSize = jsonObj.value(windowSize,
                                                  defaultWindowSize);
    if (jsonObj.contains(threshold))
    {
        for (auto& [thresholdKey, thresholdValue] : jsonObj[threshold].items())
        {
            ThresholdConfig config = ThresholdConfig();
            if (validThresholdTypes.find(thresholdKey) ==
                validThresholdTypes.end())
            {
                warning("Invalid ThresholdType: {THRESHOLD_KEY}",
                        "THRESHOLD_KEY", thresholdKey);
                continue;
            }
            auto thresholdType = validThresholdTypes.find(thresholdKey)->second;
            config.value = thresholdValue.value(thresholdValue,
                                                defaultHighThresholdValue);
            config.logMessage =
                thresholdValue.value(thresholdLog, defaultCriticalThresholdLog);
            config.target = thresholdValue.value(thresholdTarget,
                                                 defaultThresholdTarget);
            healthMetricConfig.thresholdConfigs.emplace(
                std::make_tuple(thresholdType, defaultThresholdBound), config);
        }
    }
    healthMetricConfig.path = jsonObj.value(filePath, "");
    return healthMetricConfig;
}

Json parseConfigFile(std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        warning("config JSON file not found: {PATH}", "PATH", configFile);
        return Json();
    }
    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        error("config readings JSON parser failure: {PATH}", "PATH",
              configFile);
    }
    return data;
}

std::map<MetricType, std::vector<HealthMetricConfig>> getHealthMetricConfigs()
{
    auto platformHealthConfig = parseConfigFile(HEALTH_CONFIG_FILE);
    if (!platformHealthConfig.empty())
    {
        defaultHealthMetricConfig.merge_patch(platformHealthConfig);
    }
    std::map<MetricType, std::vector<HealthMetricConfig>> healthMetricConfigs =
        {};
    for (auto& [metricName, metricInfo] : defaultHealthMetricConfig.items())
    {
        std::cout << "metric name: " << metricName << std::endl;
        std::string metricType =
            metricName.substr(0, metricName.find_first_of(metricNameDelimiter));
        if (validMetricTypes.find(metricType) == validMetricTypes.end())
        {
            warning("Invalid metric type: {METRIC_TYPE}", "METRIC_TYPE",
                    metricType);
            std::cout << "JAG Invalid metric name: " << metricName << std::endl;
            continue;
        }
        HealthMetricConfig healthMetricConfig =
            getHealthMetricConfig(metricInfo);
        healthMetricConfig.metricName = metricName;
        healthMetricConfig.metricSubtype =
            (validMetricSubtypes.find(metricName) != validMetricSubtypes.end()
                 ? validMetricSubtypes.find(metricName)->second
                 : MetricSubtype::NA);
        healthMetricConfigs[validMetricTypes.find(metricType)->second]
            .push_back(healthMetricConfig);
    }
    return healthMetricConfigs;
}

} // namespace config
} // namespace metric
} // namespace health
} // namespace phosphor
