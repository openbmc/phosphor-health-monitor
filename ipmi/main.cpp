#include "metricblob.pb.h"

#include "handler.hpp"

#include <blobs-ipmid/blobs.hpp>
#include <phosphor-logging/elog.hpp>

#include <memory>

using namespace phosphor::logging;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * This is required by the blob manager.
     * TODO: move the declaration to blobs.hpp since all handlers need it
     */
    std::unique_ptr<blobs::GenericBlobInterface> createHandler();

#ifdef __cplusplus
}
#endif

std::unique_ptr<blobs::GenericBlobInterface> createHandler()
{
    auto handler = std::make_unique<blobs::MetricBlobHandler>();
    handler->addProtobufBasedMetrics();
    return std::move(handler);
}