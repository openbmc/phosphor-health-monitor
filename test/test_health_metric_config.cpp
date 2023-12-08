#include "health_metric_config.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>

#include <iostream>

#include <gtest/gtest.h>

using namespace phosphor::health::metric::config;

constexpr auto minConfigSize = 1;

TEST(HealthMonitorConfigTest, TestConfigSize)
{
    auto healthMetricConfigs = getHealthMetricConfigs();
    EXPECT_GE(healthMetricConfigs.size(), minConfigSize);
}

bool isValidSubtype(MetricType type, MetricSubtype subType)
{
    switch (type)
    {
        case MetricType::CPU:
            return (subType == MetricSubtype::CPUTotal ||
                    subType == MetricSubtype::CPUKernel ||
                    subType == MetricSubtype::CPUUser);
        case MetricType::memory:
            return (subType == MetricSubtype::memoryAvailable ||
                    subType == MetricSubtype::memoryBufferedAndCached ||
                    subType == MetricSubtype::memoryFree ||
                    subType == MetricSubtype::memoryShared ||
                    subType == MetricSubtype::memoryTotal);
        case MetricType::storage:
            return (subType == MetricSubtype::storageReadWrite);
        case MetricType::inode:
            return (subType == MetricSubtype::NA);
        default:
            std::cout << "Metric Type: " << (int)type
                      << " Metric SubType: " << (int)subType << std::endl;
            return false;
    }
}

TEST(HealthMonitorConfigTest, TestConfigValues)
{
    auto healthMetricConfigs = getHealthMetricConfigs();
    for (const auto& [type, configs] : healthMetricConfigs)
    {
        EXPECT_NE(type, MetricType::unknown);
        EXPECT_GE(configs.size(), minConfigSize);
        for (const auto& config : configs)
        {
            EXPECT_NE(config.metricName, std::string(""));
            EXPECT_TRUE(isValidSubtype(type, config.metricSubtype));
            EXPECT_GE(config.collectionFrequency, defaultFrequency);
            EXPECT_GE(config.windowSize, defaultWindowSize);
            EXPECT_GE(config.thresholdConfigs.size(), minConfigSize);
        }
    }
}
