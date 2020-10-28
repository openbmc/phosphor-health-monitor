#include "handler.hpp"

#include <phosphor-logging/elog.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

using namespace phosphor::logging;

namespace blobs
{

namespace
{
    const std::string metricPath("/metric/snapshot");
} // namespace


bool MetricBlobHandler::canHandleBlob(const std::string& path)
{
    if (path == metricPath)
        return true;
    return false;
}

std::vector<std::string> MetricBlobHandler::getBlobIds()
{
    std::vector<std::string> result = { metricPath };
    return result;
}

bool MetricBlobHandler::deleteBlob(const std::string& path)
{
    return false;
}

bool MetricBlobHandler::stat(const std::string& path, struct BlobMeta* meta)
{
    // For unmodified gbmctool: gbmctool will query the status of the
    // Blob before opening it. The temporary value of -999 is used to
    // make the read command work, otherwise the following error
    // message will appear:
    // Caught exception from underlying implementation: Received IPMI_CC: 255
    if (metric_to_session_.find(path) == metric_to_session_.end())
    {
        metric_to_session_[path] = -999;
        return true;
    }
    else if (metric_to_session_[path] != -999)
    {
        return stat(metric_to_session_[path], meta);
    }
    else
    {
        return false;
    }
}

bool MetricBlobHandler::open(uint16_t session, uint16_t flags,
                             const std::string& path)
{
    metric_to_session_[path] = session;

    if (!canHandleBlob(path))
    {
        return false;
    }

    return false;
}

std::vector<uint8_t> MetricBlobHandler::read(uint16_t session, uint32_t offset,
                                             uint32_t requestedSize)
{
    auto it = sessions_.find(session);
    if (it == sessions_.end())
    {
        return std::vector<uint8_t>();
    }

    std::string_view result = it->second->read(offset, requestedSize);
    std::vector<uint8_t> ret(result.begin(), result.end());
    return ret;
}

bool MetricBlobHandler::write(uint16_t session, uint32_t offset,
                              const std::vector<uint8_t>& data)
{
    return false;
}

bool MetricBlobHandler::writeMeta(uint16_t session, uint32_t offset,
                                  const std::vector<uint8_t>& data)
{
    return false;
}

bool MetricBlobHandler::commit(uint16_t session,
                               const std::vector<uint8_t>& data)
{
    return false;
}

bool MetricBlobHandler::close(uint16_t session)
{
    for (std::pair<std::string, uint16_t> p : metric_to_session_)
    {
        if (session == p.second)
        {
            metric_to_session_.erase(p.first);
            break;
        }
    }

    auto it = sessions_.find(session);
    if (it == sessions_.end())
    {
        return false;
    }

    sessions_.erase(session);
    return true;
}

bool MetricBlobHandler::stat(uint16_t session, struct BlobMeta* meta)
{
    auto it = sessions_.find(session);
    if (it == sessions_.end())
    {
        return false;
    }
    bool ret = it->second->stat(meta);
    return ret;
}

bool MetricBlobHandler::expire(uint16_t session)
{
    return close(session);
}

} // namespace blobs