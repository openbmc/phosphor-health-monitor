#include "config.h"

#include "healthMonitor.hpp"

#include <sdbusplus/server/manager.hpp>
#include <sdeventplus/event.hpp>

#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

extern "C"
{
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
}

static constexpr bool DEBUG = false;
static constexpr uint8_t defaultHighThreshold = 100;

namespace phosphor
{
namespace health
{

using namespace phosphor::logging;

enum CPUStatesTime
{
    USER_IDX = 0,
    NICE_IDX,
    SYSTEM_IDX,
    IDLE_IDX,
    IOWAIT_IDX,
    IRQ_IDX,
    SOFTIRQ_IDX,
    STEAL_IDX,
    GUEST_USER_IDX,
    GUEST_NICE_IDX,
    NUM_CPU_STATES_TIME
};

double readCPUUtilization(std::string path)
{
    /* Unused var: path */
    std::ignore = path;
    std::ifstream fileStat("/proc/stat");
    if (!fileStat.is_open())
    {
        log<level::ERR>("cpu file not available",
                        entry("FILENAME = /proc/stat"));
        return -1;
    }

    std::string firstLine, labelName;
    std::size_t timeData[NUM_CPU_STATES_TIME];

    std::getline(fileStat, firstLine);
    std::stringstream ss(firstLine);
    ss >> labelName;

    if (DEBUG)
        std::cout << "CPU stats first Line is " << firstLine << "\n";

    if (labelName.compare("cpu"))
    {
        log<level::ERR>("CPU data not available");
        return -1;
    }

    int i;
    for (i = 0; i < NUM_CPU_STATES_TIME; i++)
    {
        if (!(ss >> timeData[i]))
            break;
    }

    if (i != NUM_CPU_STATES_TIME)
    {
        log<level::ERR>("CPU data not correct");
        return -1;
    }

    static double preActiveTime = 0, preIdleTime = 0;
    double activeTime, activeTimeDiff, idleTime, idleTimeDiff, totalTime,
        activePercValue;

    idleTime = timeData[IDLE_IDX] + timeData[IOWAIT_IDX];
    activeTime = timeData[USER_IDX] + timeData[NICE_IDX] +
                 timeData[SYSTEM_IDX] + timeData[IRQ_IDX] +
                 timeData[SOFTIRQ_IDX] + timeData[STEAL_IDX] +
                 timeData[GUEST_USER_IDX] + timeData[GUEST_NICE_IDX];

    idleTimeDiff = idleTime - preIdleTime;
    activeTimeDiff = activeTime - preActiveTime;

    /* Store current idle and active time for next calculation */
    preIdleTime = idleTime;
    preActiveTime = activeTime;

    totalTime = idleTimeDiff + activeTimeDiff;

    activePercValue = activeTimeDiff / totalTime * 100;

    if (DEBUG)
        std::cout << "CPU Utilization is " << activePercValue << "\n";

    return activePercValue;
}

double readMemoryUtilization(std::string path)
{
    /* Unused var: path */
    std::ignore = path;
    struct sysinfo s_info;

    sysinfo(&s_info);
    double usedRam = s_info.totalram - s_info.freeram;
    double memUsePerc = usedRam / s_info.totalram * 100;

    if (DEBUG)
    {
        std::cout << "Memory Utilization is " << memUsePerc << "\n";

        std::cout << "TotalRam: " << s_info.totalram
                  << " FreeRam: " << s_info.freeram << "\n";
        std::cout << "UseRam: " << usedRam << "\n";
    }

    return memUsePerc;
}

double readStorageUtilization(std::string path)
{

    struct statvfs buffer
    {};
    int ret = statvfs(path.c_str(), &buffer);
    double total = 0;
    double available = 0;
    double used = 0;
    double usedPercentage = 0;

    if (ret != 0)
    {
        auto e = errno;
        std::cerr << "Error from statvfs: " << strerror(e) << ",path: " << path
                  << std::endl;
        return 0;
    }

    total = buffer.f_blocks * (buffer.f_frsize / 1024);
    available = buffer.f_bfree * (buffer.f_frsize / 1024);
    used = total - available;
    usedPercentage = (used / total) * 100;

    if (DEBUG)
    {
        std::cout << "Total:" << total << "\n";
        std::cout << "Available:" << available << "\n";
        std::cout << "Used:" << used << "\n";
        std::cout << "Storage utilization is:" << usedPercentage << "\n";
    }

    return usedPercentage;
}

double readInodeUtilization(std::string path)
{

    struct statvfs buffer
    {};
    int ret = statvfs(path.c_str(), &buffer);
    double totalInodes = 0;
    double availableInodes = 0;
    double used = 0;
    double usedPercentage = 0;

    if (ret != 0)
    {
        auto e = errno;
        std::cerr << "Error from statvfs: " << strerror(e) << ",path: " << path
                  << std::endl;
        return 0;
    }

    totalInodes = buffer.f_files;
    availableInodes = buffer.f_ffree;
    used = totalInodes - availableInodes;
    usedPercentage = (used / totalInodes) * 100;

    if (DEBUG)
    {
        std::cout << "Total Inodes:" << totalInodes << "\n";
        std::cout << "Available Inodes:" << availableInodes << "\n";
        std::cout << "Used:" << used << "\n";
        std::cout << "Inodes utilization is:" << usedPercentage << "\n";
    }

    return usedPercentage;
}

constexpr auto storage = "Storage";
constexpr auto inode = "Inode";
/** Map of read function for each health sensors supported */
const std::map<std::string, std::function<double(std::string path)>>
    readSensors = {{"CPU", readCPUUtilization},
                   {"Memory", readMemoryUtilization},
                   {storage, readStorageUtilization},
                   {inode, readInodeUtilization}};

void HealthSensor::setSensorThreshold(double criticalHigh, double warningHigh)
{
    CriticalInterface::criticalHigh(criticalHigh);
    WarningInterface::warningHigh(warningHigh);
}

void HealthSensor::setSensorValueToDbus(const double value)
{
    ValueIface::value(value);
}

void HealthSensor::initHealthSensor(const std::vector<std::string>& chassisIds)
{
    std::string logMsg = sensorConfig.name + " Health Sensor initialized";
    log<level::INFO>(logMsg.c_str());

    /* Look for sensor read functions and Read Sensor values */
    double value;
    std::map<std::string,
             std::function<double(std::string path)>>::const_iterator it;
    it = readSensors.find(sensorConfig.name);

    if (sensorConfig.name.rfind(storage, 0) == 0)
    {
        it = readSensors.find(storage);
    }
    else if (sensorConfig.name.rfind(inode, 0) == 0)
    {
        it = readSensors.find(inode);
    }
    else if (it == readSensors.end())
    {
        log<level::ERR>("Sensor read function not available");
        return;
    }

    value = it->second(sensorConfig.path);

    if (value < 0)
    {
        log<level::ERR>("Reading Sensor Utilization failed",
                        entry("NAME = %s", sensorConfig.name.c_str()));
        return;
    }

    /* Initialize value queue with initial sensor reading */
    for (int i = 0; i < sensorConfig.windowSize; i++)
    {
        valQueue.push_back(value);
    }

    /* Initialize unit value (Percent) for utilization sensor */
    ValueIface::unit(ValueIface::Unit::Percent);

    setSensorValueToDbus(value);

    // Associate the sensor to chassis
    std::vector<AssociationTuple> associationTuples;
    for (const std::string_view chassisId : chassisIds)
    {
        associationTuples.emplace_back("bmc", "all_sensors", chassisId);
    }
    AssociationDefinitionInterface::associations(associationTuples);

    /* Start the timer for reading sensor data at regular interval */
    readTimer.restart(std::chrono::milliseconds(sensorConfig.freq * 1000));
}

void HealthSensor::checkSensorThreshold(const double value)
{
    if (sensorConfig.criticalHigh && (value > sensorConfig.criticalHigh))
    {
        if (!CriticalInterface::criticalAlarmHigh())
        {
            CriticalInterface::criticalAlarmHigh(true);
            if (sensorConfig.criticalLog)
                log<level::ERR>("ASSERT: Utilization Sensor has exceeded "
                                "critical high threshold",
                                entry("NAME = %s", sensorConfig.name.c_str()));
        }
    }
    else
    {
        if (CriticalInterface::criticalAlarmHigh())
        {
            CriticalInterface::criticalAlarmHigh(false);
            if (sensorConfig.criticalLog)
                log<level::INFO>("DEASSERT: Utilization Sensor is under "
                                 "critical high threshold",
                                 entry("NAME = %s", sensorConfig.name.c_str()));
        }

        /* if warning high value is not set then return */
        if (!sensorConfig.warningHigh)
            return;

        if ((value > sensorConfig.warningHigh) &&
            (!WarningInterface::warningAlarmHigh()))
        {
            WarningInterface::warningAlarmHigh(true);
            if (sensorConfig.warningLog)
                log<level::ERR>("ASSERT: Utilization Sensor has exceeded "
                                "warning high threshold",
                                entry("NAME = %s", sensorConfig.name.c_str()));
        }
        else if ((value <= sensorConfig.warningHigh) &&
                 (WarningInterface::warningAlarmHigh()))
        {
            WarningInterface::warningAlarmHigh(false);
            if (sensorConfig.warningLog)
                log<level::INFO>("DEASSERT: Utilization Sensor is under "
                                 "warning high threshold",
                                 entry("NAME = %s", sensorConfig.name.c_str()));
        }
    }
}

void HealthSensor::readHealthSensor()
{
    /* Read current sensor value */
    double value;

    if (sensorConfig.name.rfind(storage, 0) == 0)
    {
        value = readSensors.find(storage)->second(sensorConfig.path);
    }
    else if (sensorConfig.name.rfind(inode, 0) == 0)
    {
        value = readSensors.find(inode)->second(sensorConfig.path);
    }
    else
    {
        value = readSensors.find(sensorConfig.name)->second(sensorConfig.path);
    }

    if (value < 0)
    {
        log<level::ERR>("Reading Sensor Utilization failed",
                        entry("NAME = %s", sensorConfig.name.c_str()));
        return;
    }

    /* Remove first item from the queue */
    valQueue.pop_front();
    /* Add new item at the back */
    valQueue.push_back(value);

    /* Calculate average values for the given window size */
    double avgValue = 0;
    avgValue = accumulate(valQueue.begin(), valQueue.end(), avgValue);
    avgValue = avgValue / sensorConfig.windowSize;

    /* Set this new value to dbus */
    setSensorValueToDbus(avgValue);

    /* Check the sensor threshold  and log required message */
    checkSensorThreshold(avgValue);
}

void printConfig(HealthConfig& cfg)
{
    std::cout << "Name: " << cfg.name << "\n";
    std::cout << "Freq: " << (int)cfg.freq << "\n";
    std::cout << "Window Size: " << (int)cfg.windowSize << "\n";
    std::cout << "Critical value: " << (int)cfg.criticalHigh << "\n";
    std::cout << "warning value: " << (int)cfg.warningHigh << "\n";
    std::cout << "Critical log: " << (int)cfg.criticalLog << "\n";
    std::cout << "Warning log: " << (int)cfg.warningLog << "\n";
    std::cout << "Critical Target: " << cfg.criticalTgt << "\n";
    std::cout << "Warning Target: " << cfg.warningTgt << "\n\n";
    std::cout << "Path : " << cfg.path << "\n\n";
}

/* Create dbus utilization sensor object for each configured sensors */
void HealthMon::createHealthSensors(const std::vector<std::string>& chassisIds)
{
    for (auto& cfg : sensorConfigs)
    {
        std::string objPath = std::string(HEALTH_SENSOR_PATH) + cfg.name;
        auto healthSensor = std::make_shared<HealthSensor>(bus, objPath.c_str(),
                                                           cfg, chassisIds);
        healthSensors.emplace(cfg.name, healthSensor);

        std::string logMsg = cfg.name + " Health Sensor created";
        log<level::INFO>(logMsg.c_str(), entry("NAME = %s", cfg.name.c_str()));

        /* Set configured values of crtical and warning high to dbus */
        healthSensor->setSensorThreshold(cfg.criticalHigh, cfg.warningHigh);
    }
}

/** @brief Parsing Health config JSON file  */
Json HealthMon::parseConfigFile(std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        log<level::ERR>("config JSON file not found",
                        entry("FILENAME = %s", configFile.c_str()));
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        log<level::ERR>("config readings JSON parser failure",
                        entry("FILENAME = %s", configFile.c_str()));
    }

    return data;
}

void HealthMon::getConfigData(Json& data, HealthConfig& cfg)
{

    static const Json empty{};

    /* Default frerquency of sensor polling is 1 second */
    cfg.freq = data.value("Frequency", 1);

    /* Default window size sensor queue is 1 */
    cfg.windowSize = data.value("Window_size", 1);

    auto threshold = data.value("Threshold", empty);
    if (!threshold.empty())
    {
        auto criticalData = threshold.value("Critical", empty);
        if (!criticalData.empty())
        {
            cfg.criticalHigh =
                criticalData.value("Value", defaultHighThreshold);
            cfg.criticalLog = criticalData.value("Log", true);
            cfg.criticalTgt = criticalData.value("Target", "");
        }
        auto warningData = threshold.value("Warning", empty);
        if (!warningData.empty())
        {
            cfg.warningHigh = warningData.value("Value", defaultHighThreshold);
            cfg.warningLog = warningData.value("Log", false);
            cfg.warningTgt = warningData.value("Target", "");
        }
    }
    cfg.path = data.value("Path", "");
}

std::vector<HealthConfig> HealthMon::getHealthConfig()
{

    std::vector<HealthConfig> cfgs;
    HealthConfig cfg;
    auto data = parseConfigFile(HEALTH_CONFIG_FILE);

    // print values
    if (DEBUG)
        std::cout << "Config json data:\n" << data << "\n\n";

    /* Get data items from config json data*/
    for (auto& j : data.items())
    {
        auto key = j.key();
        /* key need match default value in map readSensors or match the key
         * start with "Storage" or "Inode" */
        bool isStorageOrInode =
            (key.rfind(storage, 0) == 0 || key.rfind(inode, 0) == 0);
        if (readSensors.find(key) != readSensors.end() || isStorageOrInode)
        {
            HealthConfig cfg = HealthConfig();
            cfg.name = j.key();
            getConfigData(j.value(), cfg);
            if (isStorageOrInode)
            {
                struct statvfs buffer
                {};
                int ret = statvfs(cfg.path.c_str(), &buffer);
                if (ret != 0)
                {
                    auto e = errno;
                    std::cerr << "Error from statvfs: " << strerror(e)
                              << ", name: " << cfg.name
                              << ", path: " << cfg.path
                              << ", please check your settings in config file."
                              << std::endl;
                    continue;
                }
            }
            cfgs.push_back(cfg);
            if (DEBUG)
                printConfig(cfg);
        }
        else
        {
            std::string logMsg = key + " Health Sensor not supported";
            log<level::ERR>(logMsg.c_str(), entry("NAME = %s", key.c_str()));
        }
    }
    return cfgs;
}

} // namespace health
} // namespace phosphor

/**
 * @brief Main
 */
int main()
{
    // Get a default event loop
    auto event = sdeventplus::Event::get_default();

    // Get a handle to system dbus
    auto bus = sdbusplus::bus::new_default();

    // Create an health monitor object
    phosphor::health::HealthMon healthMon(bus);

    // Request service bus name
    bus.request_name(HEALTH_BUS_NAME);

    // Add object manager to sensor node
    sdbusplus::server::manager::manager objManager(bus, SENSOR_OBJPATH);

    // Attach the bus to sd_event to service user requests
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    event.loop();

    return 0;
}
