#pragma once

#include <sdbusplus/bus.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/I2C/I2CStats/server.hpp>

#include <memory>

using I2CStatsInterface =
    sdbusplus::xyz::openbmc_project::I2C::server::I2CStats;

using AssociationTuple = std::tuple<std::string, std::string, std::string>;

using AssociationDefinitionInterface =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;

using I2CStatsObjectInterface =
    sdbusplus::server::object::object<I2CStatsInterface,
                                      AssociationDefinitionInterface>;

// Nuvoton I2C DebugFS statistics example:
// SysFS Path:    /sys/class/i2c-dev/i2c-X
// DebugFS Path:  /sys/kernel/debug/npcm_i2c/f0080000.i2c
// Files:         ber_cnt, nack_cnt, rec_fail_cnt, rec_succ_cnt, timeout_cnt

// Only physical I2C buses are included.

class I2CStatsDBusObject : I2CStatsObjectInterface
{
  public:
    I2CStatsDBusObject(const std::string& objectPath,
                       const std::string& debugfsPath, const int i2cBusId,
                       sdbusplus::bus::bus& bus,
                       const std::vector<std::string>& bmcInventoryPaths);
    void readI2CStat();

  private:
    std::string debugfsPath;
    int i2cBusId;
};

class I2CStats
{
  public:
    I2CStats(sdbusplus::bus::bus& bus);
    void initializeI2CStatsDBusObjects(
        const std::vector<std::string>& bmcInventoryPaths);
    void startReadLoop();

  private:
    sdbusplus::bus::bus& bus;
    std::vector<std::shared_ptr<I2CStatsDBusObject>> i2cStatsObjects;
    sdeventplus::Event timerEvent;
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> readTimer;
    void readI2CStats();
};
