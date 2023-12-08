#include "config.h"

#include "health_metric_config.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>

PHOSPHOR_LOG2_USING;

constexpr auto metricNameDelimiter = "_";

namespace phosphor::health::metric::config
{

using json = nlohmann::json;

// Default health metric config
json defaultHealthMetricConfig = R"({
    "CPU": {
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

constexpr auto defaultThresholdBound = ThresholdInterface::Bound::Upper;

// Valid thresholds from config
static const std::map<std::string, ThresholdInterface::Type>
    validThresholdTypes = {
        {thresholdCritical, ThresholdInterface::Type::Critical},
        {thresholdWarning, ThresholdInterface::Type::Warning}};

// Valid metrics from config
static const std::map<std::string, MetricType> validMetricTypes = {
    {"CPU", MetricType::CPU},
    {"Memory", MetricType::memory},
    {"Storage", MetricType::storage},
    {"Inode", MetricType::inode}};

// Valid submetrics from config
static const std::map<std::string, MetricSubtype> validMetricSubtypes = {
    {"CPU", MetricSubtype::CPUTotal},
    {"CPU_User", MetricSubtype::CPUUser},
    {"CPU_Kernel", MetricSubtype::CPUKernel},
    {"Memory", MetricSubtype::memoryTotal},
    {"Memory_Free", MetricSubtype::memoryFree},
    {"Memory_Available", MetricSubtype::memoryAvailable},
    {"Memory_Shared", MetricSubtype::memoryShared},
    {"Memory_Buffered_And_Cached", MetricSubtype::memoryBufferedAndCached}};

HealthMetricConfig getHealthMetricConfig(json& jsonObj)
{
    constexpr auto collectionFrequency = "Frequency";
    constexpr auto windowSize = "Window_size";
    constexpr auto threshold = "Threshold";
    constexpr auto thresholdVal = "Value";
    constexpr auto thresholdLog = "Log";
    constexpr auto thresholdTarget = "Target";
    constexpr auto filePath = "Path";
    HealthMetricConfig healthMetricConfig = {};
    healthMetricConfig.collectionFrequency = jsonObj.value(collectionFrequency,
                                                           defaultFrequency);
    healthMetricConfig.windowSize = jsonObj.value(windowSize,
                                                  defaultWindowSize);
    if (jsonObj.contains(threshold))
    {
        for (auto& [thresholdKey, thresholdValue] : jsonObj[threshold].items())
        {
            auto thresholdType = validThresholdTypes.find(thresholdKey);
            if (thresholdType == validThresholdTypes.end())
            {
                warning("Invalid ThresholdType: {THRESHOLD_KEY}",
                        "THRESHOLD_KEY", thresholdKey);
                continue;
            }
            auto config = ThresholdConfig();
            config.value = thresholdValue.value(thresholdVal,
                                                defaultHighThresholdValue);
            config.logMessage =
                thresholdValue.value(thresholdLog, defaultCriticalThresholdLog);
            config.target = thresholdValue.value(thresholdTarget,
                                                 defaultThresholdTarget);
            healthMetricConfig.thresholdConfigs.emplace(
                std::make_tuple(thresholdType->second, defaultThresholdBound),
                config);
        }
    }
    healthMetricConfig.path = jsonObj.value(filePath, "");
    return healthMetricConfig;
}

json parseConfigFile(std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        warning("config JSON file not found: {PATH}", "PATH", configFile);
        return json();
    }
    auto data = json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        error("config readings JSON parser failure: {PATH}", "PATH",
              configFile);
    }
    return data;
}

void printConfig(
    std::map<MetricType, std::vector<HealthMetricConfig>>& healthMetricConfigs)
{
    for (auto& [metricType, healthMetricConfigList] : healthMetricConfigs)
    {
        for (auto& healthMetricConfig : healthMetricConfigList)
        {
            debug(
                "MTYPE={MTYPE}, MNAME={MNAME} MSTYPE={MSTYPE} PATH={PATH}, FREQ={FREQ}, WSIZE={WSIZE}",
                "MTYPE", std::to_underlying(metricType), "MNAME",
                healthMetricConfig.metricName, "MSTYPE",
                std::to_underlying(healthMetricConfig.metricSubtype), "PATH",
                healthMetricConfig.path, "FREQ",
                healthMetricConfig.collectionFrequency, "WSIZE",
                healthMetricConfig.windowSize);
            for (auto& [thresholdKey, thresholdConfig] :
                 healthMetricConfig.thresholdConfigs)
            {
                debug(
                    "THRESHOLD TYPE={THRESHOLD_TYPE} THRESHOLD BOUND={THRESHOLD_BOUND} VALUE={VALUE} LOG={LOG} TARGET={TARGET}",
                    "THRESHOLD_TYPE", std::to_underlying(get<0>(thresholdKey)),
                    "THRESHOLD_BOUND", std::to_underlying(get<0>(thresholdKey)),
                    "VALUE", thresholdConfig.value, "LOG",
                    thresholdConfig.logMessage, "TARGET",
                    thresholdConfig.target);
            }
        }
    }
}

HealthMetricConfigs getHealthMetricConfigs()
{
    auto platformHealthConfig = parseConfigFile(HEALTH_CONFIG_FILE);
    if (!platformHealthConfig.empty())
    {
        defaultHealthMetricConfig.merge_patch(platformHealthConfig);
    }
    HealthMetricConfigs healthMetricConfigs = {};
    for (auto& [metricName, metricInfo] : defaultHealthMetricConfig.items())
    {
        std::string metricType =
            metricName.substr(0, metricName.find_first_of(metricNameDelimiter));
        auto metricTypeIter = validMetricTypes.find(metricType);
        if (metricTypeIter == validMetricTypes.end())
        {
            warning("Invalid metric type: {METRIC_TYPE}", "METRIC_TYPE",
                    metricType);
            continue;
        }
        HealthMetricConfig healthMetricConfig =
            getHealthMetricConfig(metricInfo);
        healthMetricConfig.metricName = metricName;
        auto metricSubtypeIter = validMetricSubtypes.find(metricName);
        healthMetricConfig.metricSubtype =
            (metricSubtypeIter != validMetricSubtypes.end()
                 ? metricSubtypeIter->second
                 : MetricSubtype::NA);
        healthMetricConfigs[metricTypeIter->second].push_back(
            healthMetricConfig);
    }
    printConfig(healthMetricConfigs);
    return healthMetricConfigs;
}

} // namespace phosphor::health::metric::config
