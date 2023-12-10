#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/sdbus.hpp>

#include <vector>

namespace phosphor::health::utils
{

using paths_t = std::vector<std::string>;

/** @brief Start a systemd unit */
void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit);

} // namespace phosphor::health::utils
