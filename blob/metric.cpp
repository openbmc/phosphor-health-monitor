#include "metric.hpp"

#include "metricblob.pb.h"

#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <blobs-ipmid/blobs.hpp>
#include <phosphor-logging/elog.hpp>

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

std::string ReadFileIntoString(const char* file_name)
{
    std::stringstream ss;
    std::ifstream ifs(file_name);
    while (ifs.good())
    {
        std::string line;
        std::getline(ifs, line);
        ss << line << std::endl;
    }
    return ss.str();
}

long ticks_per_sec = 0;

bool IsPIDPath(const std::string& path, int* ppid = nullptr)
{
    size_t p = path.rfind('/');
    if (p == std::string::npos)
    {
        return false;
    }
    int pid = 0;
    for (int i = p + 1; i < int(path.size()); i++)
    {
        const char ch = path[i];
        if (ch < '0' || ch > '9')
            return false;
        else
        {
            pid = pid * 10 + (ch - '0');
        }
    }
    if (ppid)
    {
        *ppid = pid;
    }
    return true;
}

std::string GetCmdLine(int pid)
{
    const std::string cmdline_path =
        "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream ifs_cmd(cmdline_path);
    std::string cmdline;
    if (ifs_cmd.good())
    {
        while (ifs_cmd.good())
        {
            char c;
            ifs_cmd.get(c);
            if (c < 32 || c >= 128)
                c = ' ';
            cmdline.push_back(c);
        }
    }
    return cmdline;
}

struct TcommUtimeStime
{
    std::string tcomm;
    float utime, stime;
};
TcommUtimeStime GetTcommUtimeStime(int pid)
{
    const std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    TcommUtimeStime ret;
    ret.tcomm = "";
    ret.utime = ret.stime = 0;

    std::ifstream ifs(stat_path);
    if (ifs.good())
    {
        const int idxes[] = {1, 13, 14}; // tcomm, utime, stime
        int ii = 0;
        std::string col;
        for (int i = 0; i < 15; i++)
        {
            ifs >> col;
            if (ii < 3 && i == idxes[ii])
            {
                switch (ii)
                {
                    case 0:
                    {
                        ret.tcomm = col;
                        break;
                    }
                    case 1:
                    case 2:
                    {
                        float t;
                        t = std::atoi(col.c_str()) * 1.0f / ticks_per_sec;
                        if (ii == 1)
                        {
                            ret.utime = t;
                        }
                        else if (ii == 2)
                        {
                            ret.stime = t;
                        }
                        break;
                    }
                }
                ii++;
            }
        }
    }
    return ret;
}

bmcmetrics::metricproto::BmcProcStatMetric BmcHealthSnapshot::DoProcStatList()
{
    const int top_n = 10;
    const std::string proc_path = "/proc/";

    bmcmetrics::metricproto::BmcProcStatMetric ret;

    struct Entry
    {
        std::string cmdline, tcomm;
        float utime, stime;
    };

    // Sort by utime + stime, no tie-breaking
    std::map<float, std::vector<Entry>> entries;

    for (const auto& proc_entry :
         std::filesystem::directory_iterator(proc_path))
    {
        const std::string& path = proc_entry.path();
        int pid = -1;
        if (IsPIDPath(path, &pid))
        {
            Entry entry;
            entry.cmdline = GetCmdLine(pid);

            TcommUtimeStime t = GetTcommUtimeStime(pid);
            entry.tcomm = t.tcomm;
            entry.utime = t.utime;
            entry.stime = t.stime;

            const float key = entry.utime + entry.stime;
            entries[key].push_back(entry);
        }
    }

    std::map<float, std::vector<Entry>>::reverse_iterator itr =
        entries.rbegin();
    std::vector<Entry>::iterator itr2 = itr->second.begin();

    int i = 0;
    bool is_others = false;
    Entry others;
    others.cmdline = "(Others)";
    others.tcomm = "";
    others.utime = others.stime = 0;

    while (true)
    {
        if (itr2 == itr->second.end())
        {
            itr++;
            if (itr == entries.rend())
                break;
            itr2 = itr->second.begin();
        }

        if (i >= top_n)
        {
            is_others = true;
        }

        Entry& ety = *itr2;
        if (is_others)
        {
            others.utime += ety.utime;
            others.stime += ety.stime;
        }
        else
        {
            bmcmetrics::metricproto::BmcProcStatMetric::BmcProcStat s;
            s.set_sidx_cmdline(getStringID(ety.cmdline));
            s.set_utime(ety.utime);
            s.set_stime(ety.stime);
            *(ret.add_stats()) = s;
        }

        itr2++;
        i++;
    }

    if (is_others)
    {
        bmcmetrics::metricproto::BmcProcStatMetric::BmcProcStat s;
        s.set_sidx_cmdline(getStringID(others.cmdline));
        s.set_utime(others.utime);
        s.set_stime(others.stime);
        *(ret.add_stats()) = s;
    }

    return ret;
}

int GetFdCount(int pid)
{
    const std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";
    int ret = 0;
    for (std::filesystem::directory_iterator itr(fd_path);
         itr != std::filesystem::directory_iterator(); itr++)
    {
        ret++;
    }
    return ret;
}

bmcmetrics::metricproto::BmcFdStatMetric BmcHealthSnapshot::DoFdStatList()
{
    const int top_n = 10;
    bmcmetrics::metricproto::BmcFdStatMetric ret;

    struct Entry
    {
        std::string cmdline, tcomm;
        int num_fd;
    };

    // Sort by fd count, no tie-breaking
    std::map<int, std::vector<Entry>> entries;

    const std::string proc_path = "/proc/";
    for (const auto& proc_entry :
         std::filesystem::directory_iterator(proc_path))
    {
        const std::string& path = proc_entry.path();
        int pid = 0;
        Entry entry;
        if (IsPIDPath(path, &pid))
        {
            try
            {
                entry.num_fd = GetFdCount(pid);
                TcommUtimeStime t = GetTcommUtimeStime(pid);
                entry.cmdline = GetCmdLine(pid);
                entry.tcomm = t.tcomm;
                const int key = entry.num_fd;
                entries[key].push_back(entry);
            }
            catch (const std::exception& e)
            { // May be caused by insufficient permissions
            }
        }
    }

    int i = 0;
    bool is_others = false;
    std::map<int, std::vector<Entry>>::reverse_iterator itr = entries.rbegin();
    std::vector<Entry>::iterator itr2 = itr->second.begin();

    Entry others;
    others.cmdline = "(Others)";
    others.tcomm = "";
    others.num_fd = 0;

    while (true)
    {
        if (itr2 == itr->second.end())
        {
            itr++;
            if (itr == entries.rend())
                break;
            itr2 = itr->second.begin();
        }

        if (i >= top_n)
        {
            is_others = true;
        }

        Entry& ety = *itr2;
        if (is_others)
        {
            others.num_fd += ety.num_fd;
        }
        else
        {
            bmcmetrics::metricproto::BmcFdStatMetric::BmcFdStat s;
            s.set_sidx_cmdline(getStringID(ety.cmdline));
            s.set_fd_count(ety.num_fd);
            *(ret.add_stats()) = s;
        }

        itr2++;
        i++;
    }

    if (is_others)
    {
        bmcmetrics::metricproto::BmcFdStatMetric::BmcFdStat s;
        s.set_sidx_cmdline(getStringID(others.cmdline));
        s.set_fd_count(others.num_fd);
        *(ret.add_stats()) = s;
    }

    return ret;
}

void BmcHealthSnapshot::doWork()
{ // In worker thread
    ticks_per_sec = sysconf(_SC_CLK_TCK);
    bmcmetrics::metricproto::BmcMetricSnapshot snapshot;

    // Memory info
    std::string buf_meminfo = ReadFileIntoString("/proc/meminfo");

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
    std::string buf_uptime = ReadFileIntoString("/proc/uptime");
    if (1)
    {
        bmcmetrics::metricproto::BmcUptimeMetric m1;
        std::stringstream ss;
        ss << buf_uptime;
        float uptime, idle_process_time;
        ss >> uptime >> idle_process_time;
        m1.set_uptime(uptime);
        m1.set_idle_process_time(idle_process_time);
        *(snapshot.mutable_uptime_metric()) = m1;
    }

    // Storage space info
    if (1)
    {
        struct statvfs fiData;
        // Lets loopyloop through the argvs
        const char* rwfs = "/";
        size_t kib = 0;
        if ((statvfs(rwfs, &fiData)) >= 0)
        {
            kib = fiData.f_bsize * fiData.f_bfree / 1024;
            bmcmetrics::metricproto::BmcDiskSpaceMetric m2;
            m2.set_rwfs_kib_available(kib);
            *(snapshot.mutable_storage_space_metric()) = m2;
        }
    }

    // Proc stat info
    if (1)
    {
        bmcmetrics::metricproto::BmcProcStatMetric m3 = DoProcStatList();
        *(snapshot.mutable_procstat_metric()) = m3;
    }

    // FD stat
    if (1)
    {
        bmcmetrics::metricproto::BmcFdStatMetric m4 = DoFdStatList();
        *(snapshot.mutable_fdstat_metric()) = m4;
    }

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
            { // SegFault here ?????
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

void BmcHealthSnapshot::StartWorkerThread()
{
    // worker_thread_ = new std::thread(&BmcHealthSnapshot::doWork, this);
    doWork();
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
