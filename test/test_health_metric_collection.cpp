#include "health_metric_collection.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <gtest/gtest.h>

namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;
namespace CollectionIntf = phosphor::health::metric::collection;

using PathInterface =
    sdbusplus::common::xyz::openbmc_project::metric::Value::namespace_path;
using ThresholdIntf =
    sdbusplus::server::xyz::openbmc_project::common::Threshold;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::StrEq;

class HealthMetricCollectionTest : public ::testing::Test
{
  public:
    sdbusplus::SdBusMock sdbusMock;
    sdbusplus::bus_t bus = sdbusplus::get_mocked_new(&sdbusMock);

    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    static constexpr auto objPath = "/xyz/openbmc_project/sdbusplus/test";
    const std::string valueInterface =
        sdbusplus::common::xyz::openbmc_project::metric::Value::interface;
    const std::string thresholdInterface =
        sdbusplus::common::xyz::openbmc_project::common::Threshold::interface;
    ConfigIntf::HealthMetric::map_t configs;

    void SetUp() override
    {
        sdbusplus::server::manager_t objManager(bus, objPath);
        bus.request_name(busName);

        configs = ConfigIntf::getHealthMetricConfigs();
        EXPECT_THAT(configs.size(), testing::Ge(1));
        // Update the health metric window size to 1 and path for test purposes
        for (auto& [key, values] : configs)
        {
            for (auto& config : values)
            {
                config.windowSize = 1;
                if (key == MetricIntf::Type::storage)
                {
                    config.path = "/tmp";
                }
            }
        }
    }

    void updateThreshold(ThresholdIntf::Bound bound, double value)
    {
        for (auto& [key, values] : configs)
        {
            for (auto& config : values)
            {
                for (auto& threshold : config.thresholds)
                {
                    if (get<ThresholdIntf::Bound>(threshold.first) == bound)
                    {
                        threshold.second.value = value;
                    }
                }
            }
        }
    }

    void createCollection()
    {
        std::map<MetricIntf::Type,
                 std::unique_ptr<CollectionIntf::HealthMetricCollection>>
            collections;
        MetricIntf::paths_t bmcPaths = {};
        for (const auto& [type, collectionConfig] : configs)
        {
            collections[type] =
                std::make_unique<CollectionIntf::HealthMetricCollection>(
                    bus, type, collectionConfig, bmcPaths);
            collections[type]->read();
        }
    }
};

TEST_F(HealthMetricCollectionTest, TestCreation)
{
    // Change threshold values to avoid threshold assertion
    updateThreshold(ThresholdIntf::Bound::Upper, 100);
    updateThreshold(ThresholdIntf::Bound::Lower, 0);

    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), NotNull(), StrEq(valueInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        // Test no signal generation for metric init properties
        const std::set<std::string> metricInitProperties = {"MaxValue",
                                                            "MinValue", "Unit"};
        EXPECT_THAT(metricInitProperties,
                    testing::Not(testing::Contains(names[0])));
        // Test signal generated for Value property set
        const std::set<std::string> metricSetProperties = {"Value"};
        EXPECT_THAT(metricSetProperties, testing::Contains(names[0]));
        return 0;
    }));

    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), NotNull(), StrEq(thresholdInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        // Test signal generated for Value property set
        EXPECT_STREQ("Value", names[0]);
        // Test no signal generation for threshold asserted
        EXPECT_STRNE("Asserted", names[0]);
        return 0;
    }));

    createCollection();
}

TEST_F(HealthMetricCollectionTest, TestThresholdAsserted)
{
    // Change threshold values to trigger threshold assertion
    updateThreshold(ThresholdIntf::Bound::Upper, 0);
    updateThreshold(ThresholdIntf::Bound::Lower, 100);

    // Test metric value property change
    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), NotNull(), StrEq(valueInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_THAT("Value", StrEq(names[0]));
        return 0;
    }));

    // Test threshold asserted property change
    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), NotNull(), StrEq(thresholdInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        // Test signal generation for threshold properties set
        const std::set<std::string> thresholdProperties = {"Value", "Asserted"};
        EXPECT_THAT(thresholdProperties, testing::Contains(names[0]));
        return 0;
    }));

    // Test AssertionChanged signal generation
    EXPECT_CALL(sdbusMock,
                sd_bus_message_new_signal(IsNull(), NotNull(), NotNull(),
                                          StrEq(thresholdInterface),
                                          StrEq("AssertionChanged")))
        .Times(6);

    createCollection();
}
