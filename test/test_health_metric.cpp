#include "health_metric.hpp"
#include "test_server.hpp"

#include <sdbusplus/test/sdbus_mock.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace std::literals;
namespace ConfigIntf = phosphor::health::metric::config;
using PathIntf =
    sdbusplus::server::xyz::openbmc_project::metric::Value::namespace_path;
using namespace phosphor::health::metric;
using namespace phosphor::health::utils;

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
    sdbusplus::bus_t bus = sdbusplus::get_mocked_new(&sdbusMock);
    static constexpr auto busName = "xyz.openbmc_project.test.HealthMon";
    const std::set<std::string> properties = {"Value", "MaxValue", "MinValue",
                                              "Unit"};
    const std::string objPath =
        std::string(PathIntf::value) + "/bmc/" + PathIntf::kernel_cpu;
    ConfigIntf::HealthMetric config;

    // The event server runs on its own context and thread, as the health
    // metric commits and resolves the events with blocking D-Bus calls.
    sdbusplus::async::context ctx;
    TestEventServer eventServer;
    sdbusplus::server::manager_t manager;
    std::atomic<bool> stopServer = false;
    std::thread serverThread;

    HealthMetricTest() :
        eventServer(ctx, objPathLogging), manager(ctx, objPathLogging)
    {
        ctx.request_name(busNameLogging);
        serverThread = std::thread([this]() {
            ctx.spawn(watchStop());
            ctx.run();
        });

        config.name = "CPU_Kernel";
        config.subType = SubType::cpuKernel;
        config.windowSize = 1;
        config.thresholds = {
            {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper},
             {.value = 90.0, .log = true, .target = ""}},
            {{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Upper},
             {.value = 80.0, .log = false, .target = ""}}};
        config.logNormalEnabled = true;
    }

    ~HealthMetricTest() noexcept override
    {
        stopServer = true;
        serverThread.join();
    }

    auto watchStop() -> sdbusplus::async::task<>
    {
        while (!stopServer)
        {
            co_await sdbusplus::async::sleep_for(ctx, 10ms);
        }
        ctx.request_stop();
    }
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
    metric->update(MValue(1275, 1500));
    // Go below warning threshold
    metric->update(MValue(1199, 1500));
}
