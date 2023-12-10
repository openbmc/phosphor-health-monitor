#pragma once

namespace phosphor
{
namespace health
{
namespace utils
{

void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit);

std::vector<std::string> findPathsWithType(sdbusplus::bus_t& bus,
                                           const std::string& iface);

} // namespace utils
} // namespace health
} // namespace phosphor
