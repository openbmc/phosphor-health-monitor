#include "config.h"

#include "healthMonitor.hpp"

#include "metrics.hpp"

#include <phosphor-logging/log.hpp>
#include <sdbusplus/server/manager.hpp>
#include <sdeventplus/event.hpp>

#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

extern "C"
{
#include <sys/sysinfo.h>
}

bool DEBUG = false;
static constexpr uint8_t defaultHighThreshold = 100;

namespace phosphor
{
namespace health
{

using namespace phosphor::logging;

/** Map of read function for each health sensors supported */
std::map<std::string, std::function<double()>> readSensors = {
    {"CPU", readCPUUtilization}, {"Memory", readMemoryUtilization}};

void HealthSensor::setSensorThreshold(double criticalHigh, double warningHigh)
{
    CriticalInterface::criticalHigh(criticalHigh);
    WarningInterface::warningHigh(warningHigh);
}

void HealthSensor::setSensorValueToDbus(const double value)
{
    ValueIface::value(value);
}

void HealthSensor::initHealthSensor()
{
    std::string logMsg = sensorConfig.name + " Health Sensor initialized";
    log<level::INFO>(logMsg.c_str());

    /* Look for sensor read functions */
    if (readSensors.find(sensorConfig.name) == readSensors.end())
    {
        log<level::ERR>("Sensor read function not available");
        return;
    }

    /* Read Sensor values */
    auto value = readSensors[sensorConfig.name]();

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
    // Note: this requires the latest sdbusplus so commented out for now
    // ValueIface::unit(ValueIface::Unit::Percent);

    setSensorValueToDbus(value);

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
    double value = readSensors[sensorConfig.name]();
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
}

/* Create dbus utilization sensor object for each configured sensors */
void HealthMon::createHealthSensors()
{
    for (auto& cfg : sensorConfigs)
    {
        std::string objPath = std::string(HEALTH_SENSOR_PATH) + cfg.name;
        auto healthSensor =
            std::make_shared<HealthSensor>(bus, objPath.c_str(), cfg);
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
}

std::vector<HealthConfig> HealthMon::getHealthConfig()
{

    std::vector<HealthConfig> cfgs;
    HealthConfig cfg;
    auto data = parseConfigFile(HEALTH_CONFIG_FILE);

    // print values
    if (DEBUG)
        std::cout << "Config json data:\n" << data << "\n\n";

    /* Get CPU config data */
    for (auto& j : data.items())
    {
        auto key = j.key();
        if (readSensors.find(key) != readSensors.end())
        {
            HealthConfig cfg = HealthConfig();
            cfg.name = j.key();
            getConfigData(j.value(), cfg);
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
