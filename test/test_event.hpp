#pragma once

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Logging/Create/aserver.hpp>
#include <xyz/openbmc_project/Logging/Entry/aserver.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class TestEventServer;
class TestEventEntry;

using EventServerIntf =
    sdbusplus::aserver::xyz::openbmc_project::logging::Create<TestEventServer>;
using EventEntryIntf =
    sdbusplus::aserver::xyz::openbmc_project::logging::Entry<TestEventEntry>;

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