#include "health_metric_collection.hpp"
#include "health_utils.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <gtest/gtest.h>

using namespace phosphor::health::metric::config;
using namespace phosphor::health::metric;
using namespace phosphor::health::metric::collection;
using namespace phosphor::health::utils;
using PathInterface =
    sdbusplus::common::xyz::openbmc_project::metrics::Value::namespace_path;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::StrEq;

constexpr auto BMCInventoryItem = "xyz.openbmc_project.Inventory.Item.Bmc";

class HealthMetricCollectionTest : public ::testing::Test
{
  public:
    sdbusplus::SdBusMock sdbusMock;
    sdbusplus::bus::bus bus = sdbusplus::get_mocked_new(&sdbusMock);

    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    static constexpr auto objPath = "/xyz/openbmc_project/sdbusplus/test";
    std::map<MetricType, std::vector<HealthMetricConfig>> healthMetricConfigs;

    void SetUp() override
    {
        healthMetricConfigs = getHealthMetricConfigs();
        EXPECT_THAT(healthMetricConfigs.size(), testing::Ge(1));
        // Update the health metric window size to 1 and path for test purposes
        for (auto& [type, configs] : healthMetricConfigs)
        {
            for (auto& config : configs)
            {
                config.windowSize = 1;
                if (type == MetricType::storage &&
                    config.metricSubtype == MetricSubtype::storageReadWrite)
                {
                    config.path = "/run/mount";
                }
            }
        }
    }
};

TEST_F(HealthMetricCollectionTest, TestHealthMetricCollectionCreation)
{
    sdbusplus::server::manager_t objManager(bus, objPath);
    bus.request_name(busName);

    const std::string valueInterface = "xyz.openbmc_project.Metric.Value";
    const std::string thresholdInterface =
        "xyz.openbmc_project.Common.Threshold";
    const std::set<std::string> valueProperties = {"Value", "MaxValue",
                                                   "MinValue", "Unit"};
    const std::set<std::string> thresholdProperties = {"Value", "Asserted"};

    std::vector<std::string> bmcInventoryPaths =
        findPathsWithType(bus, BMCInventoryItem);
    std::map<MetricType, std::unique_ptr<HealthMetricCollection>>
        healthMetricCollections;

    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), NotNull(), StrEq(valueInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_THAT(valueProperties, testing::Contains(names[0]));
        return 0;
    }));
    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), NotNull(), StrEq(thresholdInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_THAT(thresholdProperties, testing::Contains(names[0]));
        return 0;
    }));

    for (const auto& [type, configs] : healthMetricConfigs)
    {
        healthMetricCollections[type] =
            std::make_unique<HealthMetricCollection>(bus, type, configs,
                                                     bmcInventoryPaths);
        healthMetricCollections[type]->readHealthMetricCollection();
    }
}
