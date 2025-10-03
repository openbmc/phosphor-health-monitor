#include "health_metric.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ConfigIntf = phosphor::health::metric::config;
using PathIntf =
    sdbusplus::server::xyz::openbmc_project::metric::Value::namespace_path;
using ThresholdIntf =
    sdbusplus::server::xyz::openbmc_project::common::Threshold;
using namespace phosphor::health::metric;
using namespace phosphor::health::utils;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pair;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::Throw;

class HealthMetricTest : public ::testing::Test
{
  public:
    sdbusplus::SdBusMock sdbusMock;
    sdbusplus::bus_t bus = sdbusplus::get_mocked_new(&sdbusMock);
    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    const std::set<std::string> properties = {"Value", "MaxValue", "MinValue",
                                              "Unit"};
    const std::string objPath =
        std::string(PathIntf::value) + "/bmc/" + PathIntf::kernel_cpu;
    ConfigIntf::HealthMetric config;

    void SetUp() override
    {
        config.name = "CPU_Kernel";
        config.subType = SubType::cpuKernel;
        config.windowSize = 1;
        config.thresholds = {
            {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper},
             {.value = 90.0, .log = true, .target = ""}},
            {{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Upper},
             {.value = 80.0, .log = false, .target = ""}}};
        config.path = "";
    }
};

class HealthMetricEventTest : public HealthMetricEvent
{
  public:
    explicit HealthMetricEventTest(const config::HealthMetric& config) :
        HealthMetricEvent(config)
    {}

    MOCK_METHOD(sdbusplus::object_path, commitAssertLog,
                (ThresholdIntf::Type, ThresholdIntf::Bound, double, double),
                (override));
    MOCK_METHOD(void, resolveAssertLog, (const sdbusplus::object_path&),
                (override));
    MOCK_METHOD(void, commitNormalLog,
                (ThresholdIntf::Type, ThresholdIntf::Bound, double),
                (override));
};

TEST_F(HealthMetricTest, TestMetricUnmockedObjectAddRemove)
{
    sdbusplus::bus_t unmockedBus = sdbusplus::bus::new_bus();
    unmockedBus.request_name(busName);
    auto metric = std::make_unique<HealthMetric>(unmockedBus, Type::cpu, config,
                                                 paths_t());
}

TEST_F(HealthMetricTest, TestMetricThresholdChange)
{
    sdbusplus::server::manager_t objManager(bus, objPath.c_str());
    bus.request_name(busName);
    const auto thresholdProperties = std::set<std::string>{"Value", "Asserted"};

    EXPECT_CALL(sdbusMock, sd_bus_emit_properties_changed_strv(
                               IsNull(), StrEq(objPath),
                               StrEq(ValueIntf::interface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
                EXPECT_THAT(properties, testing::Contains(names[0]));
                return 0;
            }));
    EXPECT_CALL(sdbusMock, sd_bus_emit_properties_changed_strv(
                               IsNull(), StrEq(objPath),
                               StrEq(ThresholdIntf::interface), NotNull()))
        .WillRepeatedly(Invoke(
            [&]([[maybe_unused]] sd_bus* bus, [[maybe_unused]] const char* path,
                [[maybe_unused]] const char* interface, const char** names) {
                EXPECT_THAT(thresholdProperties, testing::Contains(names[0]));
                return 0;
            }));
    EXPECT_CALL(sdbusMock,
                sd_bus_message_new_signal(_, _, StrEq(objPath),
                                          StrEq(ThresholdIntf::interface),
                                          StrEq("AssertionChanged")))
        .Times(4);

    auto metric =
        std::make_unique<HealthMetric>(bus, Type::cpu, config, paths_t());
    // Exceed the critical threshold
    metric->update(MValue(1351, 1500));
    // Go below critical threshold but above warning threshold
    metric->update(MValue(1399, 1500));
    // Go below warning threshold
    metric->update(MValue(1199, 1500));
}

TEST_F(HealthMetricTest, TestThresholdEventAssertDeassert)
{
    const auto logPath =
        sdbusplus::object_path("/xyz/openbmc_project/logging/entry/1");
    HealthMetricEventTest event(config);

    {
        InSequence seq;
        EXPECT_CALL(event,
                    commitAssertLog(ThresholdIntf::Type::Critical,
                                    ThresholdIntf::Bound::Upper, 95.0, 90.0))
            .WillOnce(Return(logPath));
        EXPECT_CALL(event, resolveAssertLog(logPath)).Times(1);
        EXPECT_CALL(event, commitNormalLog(ThresholdIntf::Type::Critical,
                                           ThresholdIntf::Bound::Upper, 85.0))
            .Times(1);
    }

    event.generateThresholdEvent(ThresholdIntf::Type::Critical,
                                 ThresholdIntf::Bound::Upper, 95.0, 90.0, true);
    event.generateThresholdEvent(ThresholdIntf::Type::Critical,
                                 ThresholdIntf::Bound::Upper, 85.0, 90.0,
                                 false);
}

TEST_F(HealthMetricTest, TestThresholdEventIgnoreDuplicateAssert)
{
    const auto logPath =
        sdbusplus::object_path("/xyz/openbmc_project/logging/entry/1");
    HealthMetricEventTest event(config);

    // The second assert must not create a new log while the first one is
    // still unresolved.
    EXPECT_CALL(event, commitAssertLog(_, _, _, _)).WillOnce(Return(logPath));

    event.generateThresholdEvent(ThresholdIntf::Type::Critical,
                                 ThresholdIntf::Bound::Upper, 95.0, 90.0, true);
    event.generateThresholdEvent(ThresholdIntf::Type::Critical,
                                 ThresholdIntf::Bound::Upper, 96.0, 90.0, true);
}

TEST_F(HealthMetricTest, TestThresholdEventNormalAfterAllDeasserted)
{
    const auto criticalLog =
        sdbusplus::object_path("/xyz/openbmc_project/logging/entry/1");
    const auto warningLog =
        sdbusplus::object_path("/xyz/openbmc_project/logging/entry/2");
    HealthMetricEventTest event(config);

    EXPECT_CALL(event, commitAssertLog(ThresholdIntf::Type::Critical, _, _, _))
        .WillOnce(Return(criticalLog));
    EXPECT_CALL(event, commitAssertLog(ThresholdIntf::Type::Warning, _, _, _))
        .WillOnce(Return(warningLog));
    EXPECT_CALL(event, resolveAssertLog(criticalLog)).Times(1);
    EXPECT_CALL(event, resolveAssertLog(warningLog)).Times(1);
    // Reading normal is only logged once no threshold remains asserted.
    EXPECT_CALL(event, commitNormalLog(ThresholdIntf::Type::Critical, _, _))
        .Times(0);
    EXPECT_CALL(event, commitNormalLog(ThresholdIntf::Type::Warning,
                                       ThresholdIntf::Bound::Upper, 75.0))
        .Times(1);

    event.generateThresholdEvent(ThresholdIntf::Type::Warning,
                                 ThresholdIntf::Bound::Upper, 95.0, 80.0, true);
    event.generateThresholdEvent(ThresholdIntf::Type::Critical,
                                 ThresholdIntf::Bound::Upper, 95.0, 90.0, true);
    event.generateThresholdEvent(ThresholdIntf::Type::Critical,
                                 ThresholdIntf::Bound::Upper, 85.0, 90.0,
                                 false);
    event.generateThresholdEvent(ThresholdIntf::Type::Warning,
                                 ThresholdIntf::Bound::Upper, 75.0, 80.0,
                                 false);
}

TEST_F(HealthMetricTest, TestThresholdEventCommitFailure)
{
    HealthMetricEventTest event(config);

    EXPECT_CALL(event, commitAssertLog(_, _, _, _))
        .WillOnce(Throw(std::runtime_error("commit failed")));
    // Nothing was committed, so there is nothing to resolve.
    EXPECT_CALL(event, resolveAssertLog(_)).Times(0);
    EXPECT_CALL(event, commitNormalLog(_, _, _)).Times(1);

    EXPECT_NO_THROW(event.generateThresholdEvent(
        ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper, 95.0, 90.0,
        true));
    EXPECT_NO_THROW(event.generateThresholdEvent(
        ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper, 85.0, 90.0,
        false));
}
