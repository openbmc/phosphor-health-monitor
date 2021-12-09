#include "i2cstats.hpp"

#include "i2ctopology.hpp"

#include <phosphor-logging/log.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

const char* I2CSTATS_OBJECT_PATH = "/xyz/openbmc_project/i2cstats/";

using namespace phosphor::logging;

I2CStatsDBusObject::I2CStatsDBusObject(
    const std::string& objectPath, const std::string& debugfsPath,
    const int i2cBusId, sdbusplus::bus::bus& bus,
    const std::vector<std::string>& bmcInventoryPaths) :
    I2CStatsObjectInterface(bus, objectPath.c_str()),
    debugfsPath(debugfsPath)
{
    I2CStatsInterface::busErrorCount(0);
    I2CStatsInterface::nackCount(0);
    I2CStatsInterface::i2CSpeedInHz(0);
    I2CStatsInterface::sysfsPath("/sys/class/i2c-dev/i2c-" +
                                 std::to_string(i2cBusId));

    // Associate the I2CStats object to the BMC inventory.
    std::vector<AssociationTuple> associationTuples;
    for (const auto& bmcInventoryPath : bmcInventoryPaths)
    {
        associationTuples.push_back(
            {"bmc", "bmc_diagnostic_data", bmcInventoryPath});
    }
    AssociationDefinitionInterface::associations(associationTuples);
}

std::optional<int64_t> readFileIntoNumber(const std::string& fileName)
{
    if (!std::filesystem::exists(fileName))
    {
        log<level::ERR>((fileName + " does not exist").c_str());
        return std::nullopt;
    }
    std::ifstream ifs(fileName);
    if (!ifs.good())
    {
        log<level::ERR>(("Error reading " + fileName).c_str());
        return std::nullopt;
    }
    std::string line;
    std::getline(ifs, line);
    return std::atoll(line.c_str());
}

// Update as many values as we can manage
void I2CStatsDBusObject::readI2CStat()
{
    std::optional<int64_t> berCnt =
        readFileIntoNumber(debugfsPath + "/ber_cnt");
    if (berCnt.has_value())
    {
        I2CStatsInterface::busErrorCount(berCnt.value());
    }

    std::optional<int64_t> nackCnt =
        readFileIntoNumber(debugfsPath + "/nack_cnt");
    if (nackCnt.has_value())
    {
        I2CStatsInterface::nackCount(nackCnt.value());
    }

    std::optional<int64_t> i2cSpeed =
        readFileIntoNumber(debugfsPath + "/i2c_speed");
    if (i2cSpeed.has_value())
    {
        I2CStatsInterface::i2CSpeedInHz(i2cSpeed.value());
    }
}

I2CStats::I2CStats(sdbusplus::bus::bus& bus) :
    bus(bus), timerEvent(sdeventplus::Event::get_default()),
    readTimer(timerEvent, std::bind(&I2CStats::readI2CStats, this))
{}

void I2CStats::initializeI2CStatsDBusObjects(
    const std::vector<std::string>& bmcInventoryPaths)
{
    i2cStatsObjects.clear();
    // Scan all I2C Buses
    I2CTopologyMap m;
    m.TraverseI2C();
    std::vector<std::pair<int, std::string>> busAndPaths =
        m.GetRootBusesAndAPBAddresses();
    for (const std::pair<int, std::string>& bp : busAndPaths)
    {
        const int busId = bp.first;
        const std::string& apbPath = bp.second;

        if (apbPath.empty())
            continue; // Should not be empty

        std::string debugfsPath =
            "/sys/kernel/debug/npcm_i2c/" + apbPath + ".i2c";
        if (!std::filesystem::exists(debugfsPath))
        {
            log<level::WARNING>(("i2c-" + std::to_string(busId) + " (" +
                                 apbPath +
                                 ") does not have a corresponding debugfs path")
                                    .c_str());
            continue;
        }

        std::string objectPath =
            std::string(I2CSTATS_OBJECT_PATH) + "i2c_" + std::to_string(busId);
        log<level::INFO>(("Creating i2cstats object " + objectPath).c_str());
        puts(("Creating i2cstats object " + objectPath + "\n").c_str());
        std::shared_ptr<I2CStatsDBusObject> obj =
            std::make_shared<I2CStatsDBusObject>(objectPath, debugfsPath, busId,
                                                 bus, bmcInventoryPaths);
        i2cStatsObjects.push_back(obj);
    }
}

void I2CStats::startReadLoop()
{
    readTimer.restart(std::chrono::milliseconds(5000));
}

void I2CStats::readI2CStats()
{
    // This code assumes no I2C devices disappear and no new ones appear.
    //   If the user wants to recreate the objects, the daemon has to be
    //   restarted.
    for (std::shared_ptr<I2CStatsDBusObject> obj : i2cStatsObjects)
    {
        obj->readI2CStat();
    }
}
