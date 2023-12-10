#include "health_metric_collection.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <gtest/gtest.h>

namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;
namespace CollectionIntf = phosphor::health::metric::collection;

using PathInterface =
    sdbusplus::common::xyz::openbmc_project::metric::Value::namespace_path;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::StrEq;

class HealthMetricCollectionTest : public ::testing::Test
{
  public:
    sdbusplus::SdBusMock sdbusMock;
    sdbusplus::bus::bus bus = sdbusplus::get_mocked_new(&sdbusMock);

    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    static constexpr auto objPath = "/xyz/openbmc_project/sdbusplus/test";
    ConfigIntf::HealthMetric::map_t configs;

    void SetUp() override
    {
        configs = ConfigIntf::getHealthMetricConfigs();
        EXPECT_THAT(configs.size(), testing::Ge(1));
        // Update the health metric window size to 1 and path for test purposes
        for (auto& [key, values] : configs)
        {
            for (auto& config : values)
            {
                config.windowSize = 1;
                if (key == MetricIntf::Type::storage &&
                    config.subType == MetricIntf::SubType::storageReadWrite)
                {
                    config.path = "/tmp";
                }
            }
        }
    }
};

TEST_F(HealthMetricCollectionTest, TestHealthMetricCollectionCreation)
{
    sdbusplus::server::manager_t objManager(bus, objPath);
    bus.request_name(busName);

    const std::string valueInterface =
        sdbusplus::common::xyz::openbmc_project::metric::Value::interface;
    const std::string thresholdInterface =
        sdbusplus::common::xyz::openbmc_project::common::Threshold::interface;
    const std::set<std::string> valueProperties = {"Value", "MaxValue",
                                                   "MinValue", "Unit"};
    const std::set<std::string> thresholdProperties = {"Value", "Asserted"};
    std::map<MetricIntf::Type,
             std::unique_ptr<CollectionIntf::HealthMetricCollection>>
        collections;

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

    MetricIntf::paths_t bmcPaths = {};
    for (const auto& [type, collectionConfig] : configs)
    {
        collections[type] =
            std::make_unique<CollectionIntf::HealthMetricCollection>(
                bus, type, collectionConfig, bmcPaths);
        collections[type]->read();
    }
}
