#include "metric.hpp"

#include "metricblob.pb.h"

#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <blobs-ipmid/blobs.hpp>
#include <phosphor-logging/log.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

namespace metric_blob
{

using namespace phosphor::logging;

void BmcHealthSnapshot::doWork()
{
    bmcmetrics::metricproto::BmcMetricSnapshot snapshot;

    // Open DBus
    sd_bus* bus;
    int r;
    r = sd_bus_open_system(&bus);
    if (r < 0)
    {
        printf("Could not connect to system dbus: %s\n", strerror(-r));
    }

    // Memory info
    //
    // busctl  call xyz.openbmc_project.HealthMon
    //    /xyz/openbmc_project/sensors/utilization/Memory
    //    org.freedesktop.DBus.Properties
    //    Get ss
    //    xyz.openbmc_project.Logging.Event
    //    Message
    // std::string buf_meminfo = ReadFileIntoString("/proc/meminfo");
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply;

    r = sd_bus_call_method(bus, "xyz.openbmc_project.HealthMon",
                           "/xyz/openbmc_project/sensors/utilization/Memory",
                           "org.freedesktop.DBus.Properties", "Get", &error,
                           &reply, "ss", "xyz.openbmc_project.Logging.Event",
                           "Message");

    if (r < 0)
    {
        printf("Could not get property of the Memory health monitor object\n");
    }

    r = sd_bus_message_enter_container(reply, 'v', "s");
    if (r < 0)
    {
        printf("Could not open the message container");
    }

    const char* s;
    r = sd_bus_message_read(reply, "s", &s);
    if (r < 0)
    {
        printf("Could not read string value from message");
    }

    r = sd_bus_message_exit_container(reply);
    if (r < 0)
    {
        printf("Could not exit container");
    }

    sd_bus_close(bus);

    std::string buf_meminfo(s);

    {
        const std::unordered_map<
            std::string,
            void (bmcmetrics::metricproto::BmcMemoryMetric::*)(int)>
            kw2fn = {
                {"MemAvailable:",
                 &bmcmetrics::metricproto::BmcMemoryMetric::set_mem_available},
                {"Slab:", &bmcmetrics::metricproto::BmcMemoryMetric::set_slab},
                {"KernelStack:",
                 &bmcmetrics::metricproto::BmcMemoryMetric::set_kernel_stack},
            };
        bmcmetrics::metricproto::BmcMemoryMetric m;
        for (auto x : kw2fn)
        {
            std::string_view sv(buf_meminfo.data());
            size_t p = sv.find(x.first);
            if (p != std::string::npos)
            {
                sv = sv.substr(p + x.first.size());
                p = sv.find("kB");
                if (p != std::string::npos)
                {
                    sv = sv.substr(0, p);
                    (m.*(x.second))(std::atoi(sv.data()));
                }
            }
        }
        *(snapshot.mutable_memory_metric()) = m;
    }

    // Uptime info
    // Todo

    // Storage space info
    // Todo

    // Proc stat info
    // Todo

    // FD stat
    // Todo

    // String table
    {
        bmcmetrics::metricproto::BmcStringTable st;
        std::vector<std::string> strings(string_table_.size());
        for (const std::pair<std::string, int>& p : string_table_)
        {
            strings[p.second] = p.first;
        }
        int i = 0;
        for (const std::string& s : strings)
        {
            bmcmetrics::metricproto::BmcStringTable::StringEntry entry;
            entry.set_value(s);
            st.add_entries();
            *(st.mutable_entries(i)) = entry;
            i++;
        }
        *(snapshot.mutable_string_table()) = st;
    }

    // Save to buffer
    {
        size_t size = int(snapshot.ByteSizeLong());
        if (size > 0)
        {
            pb_dump_.resize(size);
            if (!snapshot.SerializeToArray(pb_dump_.data(), size))
            {
                log<level::ERR>("Could not serialize protobuf to array");
            }
        }
    }

    done_ = true;
}

BmcHealthSnapshot::BmcHealthSnapshot()
{
    done_ = false;
    string_id_ = 0;
}

std::string BmcHealthSnapshot::getName() const
{
    return "BmcHealthSnapshot";
};

std::string_view BmcHealthSnapshot::read(uint32_t offset,
                                         uint32_t requestedSize)
{
    return std::string_view(pb_dump_.data() + offset,
                            std::min(requestedSize, pb_dump_.size() - offset));
}

int BmcHealthSnapshot::getStringID(const std::string& s)
{
    if (string_table_.find(s) == string_table_.end())
    {
        string_table_[s] = string_id_;
        string_id_++;
    }
    return string_table_[s];
}

bool BmcHealthSnapshot::stat(struct blobs::BlobMeta* meta)
{
    // This should always be "true"
    if (!done_)
    {
        meta->blobState |= (1 << 8); // child still running
    }
    else
    {
        meta->blobState = 0;
        meta->blobState = blobs::StateFlags::open_read;
        meta->size = pb_dump_.size();
    }
    return true;
}

} // namespace metric_blob