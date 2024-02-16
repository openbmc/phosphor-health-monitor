#include "config.h"

#include "health_metric_config.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cmath>
#include <fstream>
#include <unordered_map>
#include <utility>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric::config
{

using json = nlohmann::json;

// Default health metric config
extern json defaultHealthMetricConfig;

// Valid thresholds from config
static const auto validThresholdTypes =
    std::unordered_map<std::string, ThresholdIntf::Type>{
        {"Critical", ThresholdIntf::Type::Critical},
        {"Warning", ThresholdIntf::Type::Warning}};

// Valid metrics from config
static const auto validTypes =
    std::unordered_map<std::string, Type>{{"CPU", Type::cpu},
                                          {"Memory", Type::memory},
                                          {"Storage", Type::storage},
                                          {"Inode", Type::inode}};

// Valid submetrics from config
static const auto validSubTypes = std::unordered_map<std::string, SubType>{
    {"CPU", SubType::cpuTotal},
    {"CPU_User", SubType::cpuUser},
    {"CPU_Kernel", SubType::cpuKernel},
    {"Memory", SubType::memoryTotal},
    {"Memory_Free", SubType::memoryFree},
    {"Memory_Available", SubType::memoryAvailable},
    {"Memory_Shared", SubType::memoryShared},
    {"Memory_Buffered_And_Cached", SubType::memoryBufferedAndCached},
    {"Storage_RW", SubType::storageReadWrite},
    {"Storage_TMP", SubType::storageTmp}};

/** Deserialize a Threshold from JSON. */
void from_json(const json& j, Threshold& self)
{
    self.value = j.value("Value", 100.0);
    self.log = j.value("Log", false);
    self.target = j.value("Target", Threshold::defaults::target);
}

/** Deserialize a HealthMetric from JSON. */
void from_json(const json& j, HealthMetric& self)
{
    self.collectionFreq = std::chrono::seconds(j.value(
        "Frequency",
        std::chrono::seconds(HealthMetric::defaults::frequency).count()));

    self.windowSize = j.value("Window_size",
                              HealthMetric::defaults::windowSize);
    // Path is only valid for storage
    self.path = j.value("Path", "");

    auto thresholds = j.find("Threshold");
    if (thresholds == j.end())
    {
        return;
    }

    for (auto& [key, value] : thresholds->items())
    {
        if (!validThresholdTypes.contains(key))
        {
            warning("Invalid ThresholdType: {TYPE}", "TYPE", key);
            continue;
        }

        auto config = value.template get<Threshold>();
        if (!std::isfinite(config.value))
        {
            throw std::invalid_argument("Invalid threshold value");
        }

        // ThresholdIntf::Bound::Upper is the only use case for
        // ThresholdIntf::Bound
        self.thresholds.emplace(std::make_tuple(validThresholdTypes.at(key),
                                                ThresholdIntf::Bound::Upper),
                                config);
    }
}

json parseConfigFile(std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        info("config JSON file not found: {PATH}", "PATH", configFile);
        return {};
    }

    try
    {
        return json::parse(jsonFile, nullptr, true);
    }
    catch (const json::parse_error& e)
    {
        error("Failed to parse JSON config file {PATH}: {ERROR}", "PATH",
              configFile, "ERROR", e);
    }

    return {};
}

void printConfig(HealthMetric::map_t& configs)
{
    for (auto& [type, configList] : configs)
    {
        for (auto& config : configList)
        {
            debug(
                "MTYPE={MTYPE}, MNAME={MNAME} MSTYPE={MSTYPE} PATH={PATH}, FREQ={FREQ}, WSIZE={WSIZE}",
                "MTYPE", std::to_underlying(type), "MNAME", config.name,
                "MSTYPE", std::to_underlying(config.subType), "PATH",
                config.path, "FREQ", config.collectionFreq.count(), "WSIZE",
                config.windowSize);

            for (auto& [key, threshold] : config.thresholds)
            {
                debug(
                    "THRESHOLD TYPE={TYPE} THRESHOLD BOUND={BOUND} VALUE={VALUE} LOG={LOG} TARGET={TARGET}",
                    "TYPE", std::to_underlying(get<ThresholdIntf::Type>(key)),
                    "BOUND", std::to_underlying(get<ThresholdIntf::Bound>(key)),
                    "VALUE", threshold.value, "LOG", threshold.log, "TARGET",
                    threshold.target);
            }
        }
    }
}

auto getHealthMetricConfigs() -> HealthMetric::map_t
{
    json mergedConfig(defaultHealthMetricConfig);

    if (auto platformConfig = parseConfigFile(HEALTH_CONFIG_FILE);
        !platformConfig.empty())
    {
        mergedConfig.merge_patch(platformConfig);
    }

    HealthMetric::map_t configs = {};
    for (auto& [name, metric] : mergedConfig.items())
    {
        static constexpr auto nameDelimiter = "_";
        std::string typeStr = name.substr(0, name.find_first_of(nameDelimiter));

        auto type = validTypes.find(typeStr);
        if (type == validTypes.end())
        {
            warning("Invalid metric type: {TYPE}", "TYPE", typeStr);
            continue;
        }

        auto config = metric.template get<HealthMetric>();
        config.name = name;

        auto subType = validSubTypes.find(name);
        config.subType = (subType != validSubTypes.end() ? subType->second
                                                         : SubType::NA);

        configs[type->second].emplace_back(std::move(config));
    }
    printConfig(configs);
    return configs;
}

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
    "Memory_Shared": {
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
    "Memory_Free": {
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
    "Memory_Buffered_And_Cached": {
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

} // namespace phosphor::health::metric::config
