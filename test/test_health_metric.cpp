#include "health_metric.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metrics/Value/server.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace phosphor::health::metric::config;
using namespace phosphor::health::metric;
using namespace phosphor::health::metric::config;
using PathInterface =
    sdbusplus::common::xyz::openbmc_project::metrics::Value::namespace_path;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pair;
using ::testing::StrEq;

class HealthMetricTest : public ::testing::Test
{
  public:
    sdbusplus::SdBusMock sdbusMock;
    sdbusplus::bus::bus bus = sdbusplus::get_mocked_new(&sdbusMock);
    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    const std::string valueInterface = "xyz.openbmc_project.Metrics.Value";
    const std::string thresholdInterface =
        "xyz.openbmc_project.Common.Threshold";
    const std::set<std::string> valueProperties = {"Value", "MaxValue",
                                                   "MinValue", "Unit"};
    const std::string objPath = std::string(PathInterface::value) + "/bmc/" +
                                PathInterface::kernel_cpu;
    HealthMetricConfig healthMetricConfig;

    void SetUp() override
    {
        healthMetricConfig.metricName = "CPU_Kernel";
        healthMetricConfig.metricSubtype = MetricSubtype::CPUKernel;
        healthMetricConfig.collectionFrequency = 1;
        healthMetricConfig.windowSize = 1;
        healthMetricConfig.thresholdConfigs = {
            {{ThresholdInterface::Type::Critical,
              ThresholdInterface::Bound::Upper},
             {.value = 90.0, .logMessage = true, .target = ""}},
            {{ThresholdInterface::Type::Warning,
              ThresholdInterface::Bound::Upper},
             {.value = 80.0, .logMessage = false, .target = ""}}};
        healthMetricConfig.path = "";
    }
};

TEST_F(HealthMetricTest, TestMetricUnmockedObjectAddRemove)
{
    sdbusplus::bus::bus unmokedBus = sdbusplus::bus::new_default();
    unmokedBus.request_name(busName);
    auto healthMetric = std::make_unique<HealthMetric>(
        unmokedBus, MetricType::CPU, healthMetricConfig,
        std::vector<std::string>());
}

TEST_F(HealthMetricTest, TestMetricObjectAddRemove)
{
    sdbusplus::server::manager_t objManager(bus, objPath.c_str());
    bus.request_name(busName);

    EXPECT_CALL(sdbusMock, sd_bus_emit_object_added(_, StrEq(objPath)))
        .Times(1);
    EXPECT_CALL(sdbusMock,
                sd_bus_emit_interfaces_added_strv(_, StrEq(objPath), _))
        .Times(0);
    auto healthMetric = std::make_unique<HealthMetric>(
        bus, MetricType::CPU, healthMetricConfig, std::vector<std::string>());

    // After destruction, the interface shall be removed
    EXPECT_CALL(sdbusMock, sd_bus_emit_object_removed(_, StrEq(objPath)))
        .Times(1);
    EXPECT_CALL(sdbusMock,
                sd_bus_emit_interfaces_removed_strv(_, StrEq(objPath), _))
        .Times(0);
}

TEST_F(HealthMetricTest, TestMetricSetProperties)
{
    sdbusplus::server::manager_t objManager(bus, objPath.c_str());
    bus.request_name(busName);
    const char* thresholdValueProperty = "Value";

    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), StrEq(objPath), StrEq(valueInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_THAT(valueProperties, testing::Contains(names[0]));
        return 0;
    }));
    EXPECT_CALL(sdbusMock, sd_bus_emit_properties_changed_strv(
                               IsNull(), StrEq(objPath),
                               StrEq(thresholdInterface), NotNull()))
        .WillOnce(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_STREQ(thresholdValueProperty, names[0]);
        return 0;
    }));

    auto healthMetricMock = std::make_unique<HealthMetric>(
        bus, MetricType::CPU, healthMetricConfig, std::vector<std::string>());
}

TEST_F(HealthMetricTest, TestMetricThresholdChange)
{
    sdbusplus::server::manager_t objManager(bus, objPath.c_str());
    bus.request_name(busName);
    const std::set<std::string> thresholdProperties = {"Value", "Asserted"};

    EXPECT_CALL(sdbusMock,
                sd_bus_emit_properties_changed_strv(
                    IsNull(), StrEq(objPath), StrEq(valueInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_THAT(valueProperties, testing::Contains(names[0]));
        return 0;
    }));
    EXPECT_CALL(sdbusMock, sd_bus_emit_properties_changed_strv(
                               IsNull(), StrEq(objPath),
                               StrEq(thresholdInterface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
        EXPECT_THAT(thresholdProperties, testing::Contains(names[0]));
        return 0;
    }));
    EXPECT_CALL(sdbusMock, sd_bus_message_new_signal(_, _, StrEq(objPath),
                                                     StrEq(thresholdInterface),
                                                     StrEq("AssertionChanged")))
        .Times(4);

    auto healthMetricMock = std::make_unique<HealthMetric>(
        bus, MetricType::CPU, healthMetricConfig, std::vector<std::string>());
    // Exceed the critical threshold
    healthMetricMock->updateHealthMetric(95.0);
    // Go below critical threshold but above warning threshold
    healthMetricMock->updateHealthMetric(85.0);
    // Go below warning threshold
    healthMetricMock->updateHealthMetric(75.0);
}
