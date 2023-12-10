#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/sdbus.hpp>

#include <vector>

namespace phosphor::health::utils
{

using paths_t = std::vector<std::string>;

/** @brief Start a systemd unit */
void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit);
/** @brief Find D-Bus paths for given interface */
auto findPaths(sdbusplus::bus_t& bus, const std::string& iface) -> paths_t;

} // namespace phosphor::health::utils
