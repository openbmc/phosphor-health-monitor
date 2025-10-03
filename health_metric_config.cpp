#include "config.h"

#include "health_metric_config.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cmath>
#include <fstream>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{
namespace config
{

using json = nlohmann::json;

// Default health metric config
extern json defaultHealthMetricConfig;

// Valid thresholds from config
static const auto validThresholdTypesWithBound =
    std::unordered_set<std::string>{"Critical_Lower", "Critical_Upper",
                                    "Warning_Lower", "Warning_Upper"};

static const auto validThresholdBounds =
    std::unordered_map<std::string, ThresholdIntf::Bound>{
        {"Lower", ThresholdIntf::Bound::Lower},
        {"Upper", ThresholdIntf::Bound::Upper}};

static const auto validThresholdTypes =
    std::unordered_map<std::string, ThresholdIntf::Type>{
        {"HardShutdown", ThresholdIntf::Type::HardShutdown},
        {"SoftShutdown", ThresholdIntf::Type::SoftShutdown},
        {"PerformanceLoss", ThresholdIntf::Type::PerformanceLoss},
        {"Critical", ThresholdIntf::Type::Critical},
        {"Warning", ThresholdIntf::Type::Warning}};

// Valid metrics from config
static const auto validTypes = std::unordered_map<std::string, Type>{
    {"CPU", Type::cpu},
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
    {"Storage_RW", SubType::NA},
    {"Storage_TMP", SubType::NA}};

/** Deserialize a Threshold from JSON. */
void from_json(const json& j, Threshold& self)
{
    self.value = j.value("Value", 100.0);
    self.log = j.value("Log", false);
    self.target = j.value("Target", Threshold::defaults::target);
    self.sel = j.value("SEL", false);
}

/** Deserialize a HealthMetric from JSON. */
void from_json(const json& j, HealthMetric& self)
{
    self.windowSize =
        j.value("Window_size", HealthMetric::defaults::windowSize);
    self.hysteresis = j.value("Hysteresis", HealthMetric::defaults::hysteresis);
    // Path is only valid for storage
    self.path = j.value("Path", "");

    auto thresholds = j.find("Threshold");
    if (thresholds == j.end())
    {
        return;
    }

    for (auto& [key, value] : thresholds->items())
    {
        if (!validThresholdTypesWithBound.contains(key))
        {
            warning("Invalid ThresholdType: {TYPE}", "TYPE", key);
            continue;
        }

        auto config = value.template get<Threshold>();
        if (!std::isfinite(config.value))
        {
            throw std::invalid_argument("Invalid threshold value");
        }

        static constexpr auto keyDelimiter = "_";
        std::string typeStr = key.substr(0, key.find_first_of(keyDelimiter));
        std::string boundStr =
            key.substr(key.find_last_of(keyDelimiter) + 1, key.length());

        self.thresholds.emplace(
            std::make_tuple(validThresholdTypes.at(typeStr),
                            validThresholdBounds.at(boundStr)),
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
                "TYPE={TYPE}, NAME={NAME} SUBTYPE={SUBTYPE} PATH={PATH}, WSIZE={WSIZE}, HYSTERESIS={HYSTERESIS}",
                "TYPE", type, "NAME", config.name, "SUBTYPE", config.subType,
                "PATH", config.path, "WSIZE", config.windowSize, "HYSTERESIS",
                config.hysteresis);

            for (auto& [key, threshold] : config.thresholds)
            {
                debug(
                    "THRESHOLD TYPE={TYPE} THRESHOLD BOUND={BOUND} VALUE={VALUE} LOG={LOG} TARGET={TARGET} SEL={SEL}",
                    "TYPE", get<ThresholdIntf::Type>(key), "BOUND",
                    get<ThresholdIntf::Bound>(key), "VALUE", threshold.value,
                    "LOG", threshold.log, "TARGET", threshold.target, "SEL",
                    threshold.sel);
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
        config.subType =
            (subType != validSubTypes.end() ? subType->second : SubType::NA);

        configs[type->second].emplace_back(std::move(config));
    }
    printConfig(configs);
    return configs;
}

json defaultHealthMetricConfig = R"({
    "CPU": {
        "Threshold": {
            "Critical_Upper": {
                "Value": 90.0,
                "Log": true,
                "Target": "",
                "SEL": true
            },
            "Warning_Upper": {
                "Value": 80.0,
                "Log": false,
                "Target": "",
                "SEL": false
            }
        }
    },
    "CPU_User": {
    },
    "CPU_Kernel": {
    },
    "Memory": {
    },
    "Memory_Available": {
        "Threshold": {
            "Critical_Lower": {
                "Value": 15.0,
                "Log": true,
                "Target": "",
                "SEL": true
            }
        }
    },
    "Memory_Free": {
    },
    "Memory_Shared": {
        "Threshold": {
            "Critical_Upper": {
                "Value": 85.0,
                "Log": true,
                "Target": "",
                "SEL": true
            }
        }
    },
    "Memory_Buffered_And_Cached": {
    },
    "Storage_RW": {
        "Path": "/run/initramfs/rw",
        "Threshold": {
            "Critical_Lower": {
                "Value": 15.0,
                "Log": true,
                "Target": "",
                "SEL": true
            }
        }
    },
    "Storage_TMP": {
        "Path": "/tmp",
        "Threshold": {
            "Critical_Lower": {
                "Value": 15.0,
                "Log": true,
                "Target": "",
                "SEL": true
            }
        }
    }
})"_json;

} // namespace config

namespace details
{
auto reverse_map_search(const auto& m, auto v)
{
    if (auto match = std::ranges::find_if(
            m, [=](const auto& p) { return p.second == v; });
        match != std::end(m))
    {
        return match->first;
    }
    return std::format("Enum({})", std::to_underlying(v));
}
} // namespace details

// to_string specialization for Type.
auto to_string(Type t) -> std::string
{
    return details::reverse_map_search(config::validTypes, t);
}

// to_string specialization for SubType.
auto to_string(SubType t) -> std::string
{
    return details::reverse_map_search(config::validSubTypes, t);
}

// to_string specialization for ThresholdIntf::Type.
auto to_string(ThresholdIntf::Bound t) -> std::string
{
    return details::reverse_map_search(config::validThresholdBounds, t);
}

// to_string specialization for ThresholdIntf::Type.
auto to_string(ThresholdIntf::Type t) -> std::string
{
    return details::reverse_map_search(config::validThresholdTypes, t);
}

} // namespace phosphor::health::metric
