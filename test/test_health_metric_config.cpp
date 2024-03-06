#include "health_metric_config.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>

#include <iostream>
#include <set>
#include <utility>

#include <gtest/gtest.h>

using namespace phosphor::health;
using namespace phosphor::health::metric::config;

constexpr auto minConfigSize = 1;

TEST(HealthMonitorConfigTest, TestConfigSize)
{
    auto healthMetricConfigs = getHealthMetricConfigs();
    EXPECT_GE(healthMetricConfigs.size(), minConfigSize);
}

bool isValidSubType(metric::Type type, metric::SubType subType)
{
    std::cout << "Metric Type: " << std::to_underlying(type)
              << " Metric SubType: " << std::to_underlying(subType)
              << std::endl;

    using set_t = std::set<metric::SubType>;

    switch (type)
    {
        case metric::Type::cpu:
            return set_t{metric::SubType::cpuTotal, metric::SubType::cpuKernel,
                         metric::SubType::cpuUser}
                .contains(subType);

        case metric::Type::memory:
            return set_t{metric::SubType::memoryAvailable,
                         metric::SubType::memoryBufferedAndCached,
                         metric::SubType::memoryFree,
                         metric::SubType::memoryShared,
                         metric::SubType::memoryTotal}
                .contains(subType);

        case metric::Type::storage:
        case metric::Type::inode:
            return set_t{metric::SubType::NA}.contains(subType);

        default:
            return false;
    }
}

TEST(HealthMonitorConfigTest, TestConfigValues)
{
    auto healthMetricConfigs = getHealthMetricConfigs();
    auto count_with_thresholds = 0;
    for (const auto& [type, configs] : healthMetricConfigs)
    {
        EXPECT_NE(type, metric::Type::unknown);
        EXPECT_GE(configs.size(), minConfigSize);
        for (const auto& config : configs)
        {
            EXPECT_NE(config.name, std::string(""));
            EXPECT_TRUE(isValidSubType(type, config.subType));
            EXPECT_GE(config.windowSize, HealthMetric::defaults::windowSize);
            EXPECT_GE(config.hysteresis, HealthMetric::defaults::hysteresis);
            if (config.thresholds.size())
            {
                count_with_thresholds++;
            }
        }

        EXPECT_GE(count_with_thresholds, 1);
    }
}
