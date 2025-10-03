#include "health_metric.hpp"
#include "test_server.hpp"

#include <xyz/openbmc_project/Metric/Threshold/event.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

using namespace std::literals;
using namespace phosphor::health::metric;
namespace ConfigIntf = phosphor::health::metric::config;
using EntryIntf = sdbusplus::common::xyz::openbmc_project::logging::Entry;
namespace ThresholdErrorIntf =
    sdbusplus::error::xyz::openbmc_project::metric::Threshold;
namespace ThresholdEventIntf =
    sdbusplus::event::xyz::openbmc_project::metric::Threshold;

class HealthMetricEventTest : public ::testing::Test
{
  public:
    // The event server runs on its own context and thread, as the health
    // metric commits and resolves the events with blocking D-Bus calls.
    sdbusplus::async::context ctx;
    TestEventServer eventServer;
    sdbusplus::server::manager_t manager;
    std::atomic<bool> stopServer = false;
    std::thread serverThread;

    sdbusplus::bus_t bus = sdbusplus::bus::new_bus();
    ConfigIntf::HealthMetric config;

    HealthMetricEventTest() :
        eventServer(ctx, objPathLogging), manager(ctx, objPathLogging)
    {
        ctx.request_name(busNameLogging);
        serverThread = std::thread([this]() {
            ctx.spawn(watchStop());
            ctx.run();
        });

        config.name = "CPU";
        config.subType = SubType::cpuTotal;
        config.windowSize = 1;
        config.thresholds = {
            {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Upper},
             {.value = 90.0, .log = true, .target = ""}},
            {{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Upper},
             {.value = 80.0, .log = false, .target = ""}}};
        config.logNormalEnabled = true;
    }

    ~HealthMetricEventTest() noexcept override
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

    auto createMetric() -> std::unique_ptr<HealthMetric>
    {
        return std::make_unique<HealthMetric>(bus, Type::cpu, config,
                                              paths_t());
    }

    static auto isResolved(const std::string& path) -> bool
    {
        auto b = sdbusplus::bus::new_default();
        auto m = b.new_method_call(busNameLogging, path.c_str(),
                                   "org.freedesktop.DBus.Properties", "Get");
        m.append(EntryIntf::interface, "Resolved");
        auto reply = b.call(m);
        return std::get<bool>(reply.unpack<std::variant<bool>>());
    }
};

TEST_F(HealthMetricEventTest, TestCriticalAssertDeassert)
{
    auto metric = createMetric();

    // Exceed the critical threshold.
    metric->update(MValue(95, 100));
    auto events = eventServer.createdEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].message,
              ThresholdErrorIntf::TriggerNumericAboveUpperCritical::errName);
    EXPECT_EQ(events[0].additionalData["METRIC_NAME"],
              "/xyz/openbmc_project/metric/bmc/cpu/total");
    EXPECT_EQ(events[0].additionalData["TRIGGER_NAME"],
              "/xyz/openbmc_project/metric/bmc/cpu/total");
    EXPECT_FALSE(isResolved(events[0].path));

    // Stay above the critical threshold, no duplicate event.
    metric->update(MValue(96, 100));
    EXPECT_EQ(eventServer.createdEvents().size(), 1);

    // Go below the critical threshold.
    metric->update(MValue(70, 100));
    events = eventServer.createdEvents();
    EXPECT_TRUE(isResolved(events[0].path));
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[1].message,
              ThresholdEventIntf::TriggerNumericReadingNormal::errName);
}

TEST_F(HealthMetricEventTest, TestCriticalDeassertWithinWarning)
{
    auto metric = createMetric();

    // Exceed both the critical and the (unlogged) warning thresholds.
    metric->update(MValue(95, 100));
    auto events = eventServer.createdEvents();
    ASSERT_EQ(events.size(), 1);

    // Go below the critical threshold but stay within the warning range, the
    // critical event is resolved but the reading is not normal yet.
    metric->update(MValue(85, 100));
    events = eventServer.createdEvents();
    EXPECT_TRUE(isResolved(events[0].path));
    EXPECT_EQ(events.size(), 1);

    // Go below the warning threshold.
    metric->update(MValue(70, 100));
    events = eventServer.createdEvents();
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[1].message,
              ThresholdEventIntf::TriggerNumericReadingNormal::errName);
    EXPECT_EQ(events[1].additionalData["TRIGGER_NAME"],
              "/xyz/openbmc_project/metric/bmc/cpu/total");
}

TEST_F(HealthMetricEventTest, TestCriticalAndWarningLogged)
{
    config
        .thresholds[{ThresholdIntf::Type::Warning, ThresholdIntf::Bound::Upper}]
        .log = true;
    auto metric = createMetric();

    // Exceed both the critical and the warning thresholds.
    metric->update(MValue(95, 100));
    auto events = eventServer.createdEvents();
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[0].message,
              ThresholdErrorIntf::TriggerNumericAboveUpperCritical::errName);
    EXPECT_EQ(events[1].message,
              ThresholdErrorIntf::TriggerNumericAboveUpperWarning::errName);

    // Go below the critical threshold but stay within the warning range, the
    // warning event stays outstanding.
    metric->update(MValue(85, 100));
    events = eventServer.createdEvents();
    EXPECT_EQ(events.size(), 2);
    EXPECT_TRUE(isResolved(events[0].path));
    EXPECT_FALSE(isResolved(events[1].path));

    // Go below the warning threshold.
    metric->update(MValue(70, 100));
    events = eventServer.createdEvents();
    EXPECT_TRUE(isResolved(events[1].path));
    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[2].message,
              ThresholdEventIntf::TriggerNumericReadingNormal::errName);
}

TEST_F(HealthMetricEventTest, TestLowerBound)
{
    config.thresholds = {
        {{ThresholdIntf::Type::Critical, ThresholdIntf::Bound::Lower},
         {.value = 15.0, .log = true, .target = ""}}};
    auto metric = createMetric();

    metric->update(MValue(10, 100));
    auto events = eventServer.createdEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].message,
              ThresholdErrorIntf::TriggerNumericBelowLowerCritical::errName);
    EXPECT_EQ(events[0].additionalData["TRIGGER_NAME"],
              "/xyz/openbmc_project/metric/bmc/cpu/total");

    metric->update(MValue(20, 100));
    events = eventServer.createdEvents();
    EXPECT_TRUE(isResolved(events[0].path));
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[1].message,
              ThresholdEventIntf::TriggerNumericReadingNormal::errName);
}

TEST_F(HealthMetricEventTest, TestLogDisabled)
{
    config
        .thresholds[{ThresholdIntf::Type::Critical,
                     ThresholdIntf::Bound::Upper}]
        .log = false;
    config.logNormalEnabled = false;
    auto metric = createMetric();

    metric->update(MValue(95, 100));
    metric->update(MValue(70, 100));
    EXPECT_TRUE(eventServer.createdEvents().empty());
}
