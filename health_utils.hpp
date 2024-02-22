#pragma once

#include <sdbusplus/async.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/sdbus.hpp>

#include <vector>

namespace phosphor::health::utils
{

using paths_t = std::vector<std::string>;

/** @brief Start a systemd unit */
void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit);
/** @brief Find D-Bus paths for given interface */
auto findPaths(sdbusplus::async::context& ctx, const std::string& iface,
               const std::string& subpath) -> sdbusplus::async::task<paths_t>;

} // namespace phosphor::health::utils
