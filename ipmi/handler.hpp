#pragma once

#include "metric.hpp"

#include <google/protobuf/stubs/common.h>

#include <blobs-ipmid/blobs.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

namespace blobs
{

class MetricBlobHandler : public GenericBlobInterface
{
  public:
    MetricBlobHandler() = default;
    ~MetricBlobHandler() = default;
    MetricBlobHandler(const MetricBlobHandler&) = delete;
    MetricBlobHandler& operator=(const MetricBlobHandler&) = delete;
    MetricBlobHandler(MetricBlobHandler&&) = default;
    MetricBlobHandler& operator=(MetricBlobHandler&&) = default;

    bool canHandleBlob(const std::string& path) override;
    std::vector<std::string> getBlobIds() override;
    bool deleteBlob(const std::string& path) override;
    bool stat(const std::string& path, struct BlobMeta* meta) override;
    bool open(uint16_t session, uint16_t flags,
              const std::string& path) override;
    std::vector<uint8_t> read(uint16_t session, uint32_t offset,
                              uint32_t requestedSize) override;
    bool write(uint16_t session, uint32_t offset,
               const std::vector<uint8_t>& data) override;
    bool writeMeta(uint16_t session, uint32_t offset,
                   const std::vector<uint8_t>& data) override;
    bool commit(uint16_t session, const std::vector<uint8_t>& data) override;
    bool close(uint16_t session) override;
    bool stat(uint16_t session, struct BlobMeta* meta) override;
    bool expire(uint16_t session) override;

    void addProtobufBasedMetrics();

  private:
    /* map of sessionId: open metric object pointer. */
    std::unordered_map<uint16_t, std::unique_ptr<metric_blob::MetricInterface>>
        sessions_;
    std::unordered_map<std::string, uint16_t> metric_to_session_;
};

} // namespace blobs