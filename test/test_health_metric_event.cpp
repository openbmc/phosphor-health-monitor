#include "health_metric.hpp"

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Logging/Create/aserver.hpp>
#include <xyz/openbmc_project/Logging/Entry/aserver.hpp>
#include <xyz/openbmc_project/Metric/Threshold/event.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

class TestEventServer;
class TestEventEntry;

using namespace std::literals;
using namespace phosphor::health::metric;
namespace ConfigIntf = phosphor::health::metric::config;
using EventServerIntf =
    sdbusplus::aserver::xyz::openbmc_project::logging::Create<TestEventServer>;
using EventEntryIntf =
    sdbusplus::aserver::xyz::openbmc_project::logging::Entry<TestEventEntry>;
using EntryIntf = sdbusplus::common::xyz::openbmc_project::logging::Entry;
namespace ThresholdErrorIntf =
    sdbusplus::error::xyz::openbmc_project::metric::Threshold;
namespace ThresholdEventIntf =
    sdbusplus::event::xyz::openbmc_project::metric::Threshold;

// Test event entry to receive the resolve requests.
class TestEventEntry : public EventEntryIntf
{
  public:
    TestEventEntry(sdbusplus::async::context& ctx, const char* path) :
        EventEntryIntf(ctx, path,
                       EventEntryIntf::signal_action::emit_object_added)
    {}

    auto method_call(get_entry_t)
        -> sdbusplus::async::task<get_entry_t::return_type>
    {
        get_entry_t::return_type fd = 0;
        co_return fd;
    }
};

// Test event server to stand in for phosphor-logging.
class TestEventServer : public EventServerIntf
{
  public:
    struct CreatedEvent
    {
        std::string message;
        std::map<std::string, std::string> additionalData;
        std::string path;
    };

    TestEventServer(sdbusplus::async::context& ctx, const char* path) :
        EventServerIntf(ctx, path,
                        EventServerIntf::signal_action::emit_object_added),
        ctx(ctx)
    {}

    auto method_call(create_t, auto message, auto, auto additionalData)
        -> sdbusplus::async::task<create_t::return_type>
    {
        std::lock_guard lock(eventsLock);
        auto path = std::string(EntryIntf::namespace_path::value) + "/" +
                    std::to_string(events.size() + 1);
        events.emplace_back(message, additionalData, path);
        entries.emplace_back(
            std::make_unique<TestEventEntry>(ctx, path.c_str()));
        co_return sdbusplus::object_path(path);
    }

    auto method_call(create_with_ffdc_files_t, auto, auto, auto, auto)
        -> sdbusplus::async::task<create_with_ffdc_files_t::return_type>
    {
        co_return sdbusplus::object_path{};
    }

    auto createdEvents() -> std::vector<CreatedEvent>
    {
        std::lock_guard lock(eventsLock);
        return events;
    }

  private:
    sdbusplus::async::context& ctx;
    std::mutex eventsLock;
    std::vector<CreatedEvent> events;
    std::vector<std::unique_ptr<TestEventEntry>> entries;
};

class HealthMetricEventTest : public ::testing::Test
{
  public:
    static constexpr auto serviceName = "xyz.openbmc_project.Logging";
    static constexpr auto objectPath = "/xyz/openbmc_project/logging";

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
        eventServer(ctx, objectPath), manager(ctx, objectPath)
    {
        ctx.request_name(serviceName);
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
        auto m = b.new_method_call(serviceName, path.c_str(),
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
