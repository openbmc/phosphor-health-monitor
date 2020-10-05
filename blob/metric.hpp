#pragma once

#include <unistd.h>

#include <blobs-ipmid/blobs.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
//#include <thread>

#include "metricblob.pb.h"

using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

namespace metric_blob
{

/**
 * @class MetricInterface is an abstraction for a storage location.
 *     Each instance would be uniquely identified by a baseId string.
 */
class MetricInterface
{
  public:
    virtual ~MetricInterface() = default;

    /**
     * @returns name string of the metric.
     */
    virtual std::string getName() const = 0;

    /**
     * Reads data from this metric
     * @param offset: offset into the data to read
     * @param requestedSize: how many bytes to read
     * @returns Bytes able to read. Returns empty if nothing can be read.
     */
    virtual std::string_view read(uint32_t offset, uint32_t requestedSize) = 0;

    /**
     * Returns information about the amount of readable data and whether the
     * metric has finished populating.
     * @param meta: Struct to fill with the metadata info
     */
    virtual bool stat(struct blobs::BlobMeta* meta) = 0;
};

class BmcHealthSnapshot : public MetricInterface
{
  public:
    BmcHealthSnapshot();
    void StartWorkerThread();

    std::string getName() const override;
    std::string_view read(uint32_t offset, uint32_t requestedSize) override;
    bool stat(struct blobs::BlobMeta* meta) override;
    void doWork();

  private:
    bmcmetrics::metricproto::BmcProcStatMetric DoProcStatList();
    bmcmetrics::metricproto::BmcFdStatMetric DoFdStatList();
    int getStringID(const std::string& s);
    // std::thread* worker_thread_;
    std::atomic<bool> done_;
    std::vector<char> pb_dump_;
    std::unordered_map<std::string, int> string_table_;
    int string_id_;
};

} // namespace metric_blob
