#include "config.h"

#include "healthMonitor.hpp"

#include <unistd.h>

#include <boost/asio/deadline_timer.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/sd_event.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/server/manager.hpp>
#include <sdeventplus/event.hpp>

#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>

extern "C"
{
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
}

PHOSPHOR_LOG2_USING;

static constexpr bool DEBUG = false;
static constexpr uint8_t defaultHighThreshold = 100;

// Limit sensor recreation interval to 10s
bool needUpdate;
static constexpr int TIMER_INTERVAL = 10;
std::shared_ptr<boost::asio::deadline_timer> sensorRecreateTimer;
std::shared_ptr<phosphor::health::HealthMon> healthMon;

namespace phosphor
{
namespace health
{

// Example values for iface:
// BMC_CONFIGURATION
// BMC_INVENTORY_ITEM
std::vector<std::string> findPathsWithType(sdbusplus::bus_t& bus,
                                           const std::string& iface)
{
    PHOSPHOR_LOG2_USING;
    std::vector<std::string> ret;

    // Find all BMCs (DBus objects implementing the
    // Inventory.Item.Bmc interface that may be created by
    // configuring the Inventory Manager)
    sdbusplus::message_t msg = bus.new_method_call(
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths");

    // "/": No limit for paths for all the paths that may be touched
    // in this daemon

    // 0: Limit the depth to 0 to match both objects created by
    // EntityManager and by InventoryManager

    // {iface}: The endpoint of the Association Definition must have
    // the Inventory.Item.Bmc interface
    msg.append("/", 0, std::vector<std::string>{iface});

    try
    {
        bus.call(msg, 0).read(ret);

        if (!ret.empty())
        {
            debug("{IFACE} found", "IFACE", iface);
        }
        else
        {
            debug("{IFACE} not found", "IFACE", iface);
        }
    }
    catch (std::exception& e)
    {
        error("Exception occurred while calling {PATH}: {ERROR}", "PATH",
              InventoryPath, "ERROR", e);
    }
    return ret;
}

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

enum CPUUtilizationType
{
    USER = 0,
    KERNEL,
    TOTAL
};

double readCPUUtilization(enum CPUUtilizationType type)
{
    auto proc_stat = "/proc/stat";
    std::ifstream fileStat(proc_stat);
    if (!fileStat.is_open())
    {
        error("cpu file not available: {PATH}", "PATH", proc_stat);
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
        error("CPU data not available");
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
        error("CPU data not correct");
        return -1;
    }

    static std::unordered_map<enum CPUUtilizationType, double> preActiveTime,
        preIdleTime;
    double activeTime, activeTimeDiff, idleTime, idleTimeDiff, totalTime,
        activePercValue;

    idleTime = timeData[IDLE_IDX] + timeData[IOWAIT_IDX];
    if (type == TOTAL)
    {
        activeTime = timeData[USER_IDX] + timeData[NICE_IDX] +
                     timeData[SYSTEM_IDX] + timeData[IRQ_IDX] +
                     timeData[SOFTIRQ_IDX] + timeData[STEAL_IDX] +
                     timeData[GUEST_USER_IDX] + timeData[GUEST_NICE_IDX];
    }
    else if (type == KERNEL)
    {
        activeTime = timeData[SYSTEM_IDX];
    }
    else if (type == USER)
    {
        activeTime = timeData[USER_IDX];
    }

    idleTimeDiff = idleTime - preIdleTime[type];
    activeTimeDiff = activeTime - preActiveTime[type];

    /* Store current idle and active time for next calculation */
    preIdleTime[type] = idleTime;
    preActiveTime[type] = activeTime;

    totalTime = idleTimeDiff + activeTimeDiff;

    activePercValue = activeTimeDiff / totalTime * 100;

    if (DEBUG)
        std::cout << "CPU Utilization is " << activePercValue << "\n";

    return activePercValue;
}

auto readCPUUtilizationTotal([[maybe_unused]] const std::string& path)
{
    return readCPUUtilization(CPUUtilizationType::TOTAL);
}

auto readCPUUtilizationKernel([[maybe_unused]] const std::string& path)
{
    return readCPUUtilization(CPUUtilizationType::KERNEL);
}

auto readCPUUtilizationUser([[maybe_unused]] const std::string& path)
{
    return readCPUUtilization(CPUUtilizationType::USER);
}

double readMemoryUtilization([[maybe_unused]] const std::string& path)
{
    /* Unused var: path */
    std::ignore = path;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    double memTotal = -1;
    double memAvail = -1;

    while (std::getline(meminfo, line))
    {
        std::string name;
        double value;
        std::istringstream iss(line);

        if (!(iss >> name >> value))
        {
            continue;
        }

        if (name.starts_with("MemTotal"))
        {
            memTotal = value;
        }
        else if (name.starts_with("MemAvailable"))
        {
            memAvail = value;
        }
    }

    if (memTotal <= 0 || memAvail <= 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (DEBUG)
    {
        std::cout << "MemTotal: " << memTotal << " MemAvailable: " << memAvail
                  << std::endl;
    }

    return (memTotal - memAvail) / memTotal * 100;
}

double readStorageUtilization([[maybe_unused]] const std::string& path)
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

double readInodeUtilization([[maybe_unused]] const std::string& path)
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

/** Map of read function for each health sensors supported
 *
 * The following health sensors are read in the ManagerDiagnosticData
 * Redfish resource:
 *  - CPU_Kernel populates ProcessorStatistics.KernelPercent
 *  - CPU_User populates ProcessorStatistics.UserPercent
 */
const std::map<std::string, std::function<double(const std::string& path)>>
    readSensors = {{"CPU", readCPUUtilizationTotal},
                   {"CPU_Kernel", readCPUUtilizationKernel},
                   {"CPU_User", readCPUUtilizationUser},
                   {"Memory", readMemoryUtilization},
                   {storage, readStorageUtilization},
                   {inode, readInodeUtilization}};

void HealthSensor::setSensorThreshold(double criticalHigh, double warningHigh)
{
    CriticalInterface::criticalHigh(criticalHigh);
    CriticalInterface::criticalLow(std::numeric_limits<double>::quiet_NaN());

    WarningInterface::warningHigh(warningHigh);
    WarningInterface::warningLow(std::numeric_limits<double>::quiet_NaN());
}

void HealthSensor::setSensorValueToDbus(const double value)
{
    ValueIface::value(value);
}

void HealthSensor::initHealthSensor(
    const std::vector<std::string>& bmcInventoryPaths)
{
    info("{SENSOR} Health Sensor initialized", "SENSOR", sensorConfig.name);

    /* Look for sensor read functions and Read Sensor values */
    auto it = readSensors.find(sensorConfig.name);

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
        error("Sensor read function not available");
        return;
    }

    double value = it->second(sensorConfig.path);

    if (value < 0)
    {
        error("Reading Sensor Utilization failed: {SENSOR}", "SENSOR",
              sensorConfig.name);
        return;
    }

    /* Initialize unit value (Percent) for utilization sensor */
    ValueIface::unit(ValueIface::Unit::Percent);

    ValueIface::maxValue(100);
    ValueIface::minValue(0);
    ValueIface::value(std::numeric_limits<double>::quiet_NaN());

    // Associate the sensor to chassis
    // This connects the DBus object to a Chassis.

    std::vector<AssociationTuple> associationTuples;
    for (const auto& chassisId : bmcInventoryPaths)
    {
        // This utilization sensor "is monitoring" the BMC with path chassisId.
        // The chassisId is "monitored_by" this utilization sensor.
        associationTuples.push_back({"monitors", "monitored_by", chassisId});
    }
    AssociationDefinitionInterface::associations(associationTuples);

    /* Start the timer for reading sensor data at regular interval */
    readTimer.restart(std::chrono::milliseconds(sensorConfig.freq * 1000));
}

void HealthSensor::checkSensorThreshold(const double value)
{
    if (std::isfinite(sensorConfig.criticalHigh) &&
        (value > sensorConfig.criticalHigh))
    {
        if (!CriticalInterface::criticalAlarmHigh())
        {
            CriticalInterface::criticalAlarmHigh(true);
            if (sensorConfig.criticalLog)
            {
                error(
                    "ASSERT: sensor {SENSOR} is above the upper threshold critical high",
                    "SENSOR", sensorConfig.name);
                startUnit(sensorConfig.criticalTgt);
            }
        }
        return;
    }

    if (CriticalInterface::criticalAlarmHigh())
    {
        CriticalInterface::criticalAlarmHigh(false);
        if (sensorConfig.criticalLog)
            info(
                "DEASSERT: sensor {SENSOR} is under the upper threshold critical high",
                "SENSOR", sensorConfig.name);
    }

    if (std::isfinite(sensorConfig.warningHigh) &&
        (value > sensorConfig.warningHigh))
    {
        if (!WarningInterface::warningAlarmHigh())
        {
            WarningInterface::warningAlarmHigh(true);
            if (sensorConfig.warningLog)
            {
                error(
                    "ASSERT: sensor {SENSOR} is above the upper threshold warning high",
                    "SENSOR", sensorConfig.name);
                startUnit(sensorConfig.warningTgt);
            }
        }
        return;
    }

    if (WarningInterface::warningAlarmHigh())
    {
        WarningInterface::warningAlarmHigh(false);
        if (sensorConfig.warningLog)
            info(
                "DEASSERT: sensor {SENSOR} is under the upper threshold warning high",
                "SENSOR", sensorConfig.name);
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
        error("Reading Sensor Utilization failed: {SENSOR}", "SENSOR",
              sensorConfig.name);
        return;
    }

    /* Remove first item from the queue */
    if (valQueue.size() >= sensorConfig.windowSize)
    {
        valQueue.pop_front();
    }
    /* Add new item at the back */
    valQueue.push_back(value);
    /* Wait until the queue is filled with enough reference*/
    if (valQueue.size() < sensorConfig.windowSize)
    {
        return;
    }

    /* Calculate average values for the given window size */
    double avgValue = 0;
    avgValue = accumulate(valQueue.begin(), valQueue.end(), avgValue);
    avgValue = avgValue / sensorConfig.windowSize;

    /* Set this new value to dbus */
    setSensorValueToDbus(avgValue);

    /* Check the sensor threshold  and log required message */
    checkSensorThreshold(avgValue);
}

void HealthSensor::startUnit(const std::string& sysdUnit)
{
    if (sysdUnit.empty())
    {
        return;
    }

    sdbusplus::message_t msg = bus.new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    msg.append(sysdUnit, "replace");
    bus.call_noreply(msg);
}

void HealthMon::recreateSensors()
{
    PHOSPHOR_LOG2_USING;
    healthSensors.clear();

    // Find BMC inventory paths and create health sensors
    std::vector<std::string> bmcInventoryPaths =
        findPathsWithType(bus, BMC_INVENTORY_ITEM);
    createHealthSensors(bmcInventoryPaths);
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
void HealthMon::createHealthSensors(
    const std::vector<std::string>& bmcInventoryPaths)
{
    for (auto& cfg : sensorConfigs)
    {
        std::string objPath = std::string(HEALTH_SENSOR_PATH) + cfg.name;
        auto healthSensor = std::make_shared<HealthSensor>(
            bus, objPath.c_str(), cfg, bmcInventoryPaths);
        healthSensors.emplace(cfg.name, healthSensor);

        info("{SENSOR} Health Sensor created", "SENSOR", cfg.name);

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
        error("config JSON file not found: {PATH}", "PATH", configFile);
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        error("config readings JSON parser failure: {PATH}", "PATH",
              configFile);
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
            error("{SENSOR} Health Sensor not supported", "SENSOR", key);
        }
    }
    return cfgs;
}

// Two caveats here.
// 1. The BMC Inventory will only show up by the nearest ObjectMapper polling
// interval.
// 2. InterfacesAdded events will are not emitted like they are with E-M.
void HealthMon::createBmcInventoryIfNotCreated()
{
    if (bmcInventory == nullptr)
    {
        info("createBmcInventory");
        bmcInventory = std::make_shared<phosphor::health::BmcInventory>(
            bus, "/xyz/openbmc_project/inventory/bmc");
    }
}

bool HealthMon::bmcInventoryCreated()
{
    return bmcInventory != nullptr;
}

} // namespace health
} // namespace phosphor

void sensorRecreateTimerCallback(
    std::shared_ptr<boost::asio::deadline_timer> timer, sdbusplus::bus_t& bus)
{
    timer->expires_from_now(boost::posix_time::seconds(TIMER_INTERVAL));
    timer->async_wait([timer, &bus](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted)
        {
            info("sensorRecreateTimer aborted");
            return;
        }

        // When Entity-manager is already running
        if (!needUpdate)
        {
            if ((!healthMon->bmcInventoryCreated()) &&
                (!phosphor::health::findPathsWithType(bus, BMC_CONFIGURATION)
                      .empty()))
            {
                healthMon->createBmcInventoryIfNotCreated();
                needUpdate = true;
            }
        }
        else
        {

            // If this daemon maintains its own DBus object, we must make sure
            // the object is registered to ObjectMapper
            if (phosphor::health::findPathsWithType(bus, BMC_INVENTORY_ITEM)
                    .empty())
            {
                info(
                    "BMC inventory item not registered to Object Mapper yet, waiting for next iteration");
            }
            else
            {
                info(
                    "BMC inventory item registered to Object Mapper, creating sensors now");
                healthMon->recreateSensors();
                needUpdate = false;
            }
        }
        sensorRecreateTimerCallback(timer, bus);
    });
}

/**
 * @brief Main
 */
int main()
{
    // The io_context is needed for the timer
    boost::asio::io_context io;

    // DBus connection
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    conn->request_name(HEALTH_BUS_NAME);

    // Get a default event loop
    auto event = sdeventplus::Event::get_default();

    // Create an health monitor object
    healthMon = std::make_shared<phosphor::health::HealthMon>(*conn);

    // Add object manager through object_server
    sdbusplus::asio::object_server objectServer(conn);

    sdbusplus::asio::sd_event_wrapper sdEvents(io);

    sensorRecreateTimer = std::make_shared<boost::asio::deadline_timer>(io);

    // If the SystemInventory does not exist: wait for the InterfaceAdded signal
    auto interfacesAddedSignalHandler = std::make_unique<
        sdbusplus::bus::match_t>(
        static_cast<sdbusplus::bus_t&>(*conn),
        sdbusplus::bus::match::rules::interfacesAdded(),
        [conn](sdbusplus::message_t& msg) {
            using Association =
                std::tuple<std::string, std::string, std::string>;
            using InterfacesAdded = std::vector<std::pair<
                std::string,
                std::vector<std::pair<
                    std::string, std::variant<std::vector<Association>>>>>>;

            sdbusplus::message::object_path o;
            InterfacesAdded interfacesAdded;

            try
            {
                msg.read(o);
                msg.read(interfacesAdded);
            }
            catch (const std::exception& e)
            {
                error(
                    "Exception occurred while processing interfacesAdded:  {EXCEPTION}",
                    "EXCEPTION", e.what());
                return;
            }

            // Ignore any signal coming from health-monitor itself.
            if (msg.get_sender() == conn->get_unique_name())
            {
                return;
            }

            // Check if the BMC Inventory is in the interfaces created.
            bool hasBmcConfiguration = false;
            for (const auto& x : interfacesAdded)
            {
                if (x.first == BMC_CONFIGURATION)
                {
                    hasBmcConfiguration = true;
                }
            }

            if (hasBmcConfiguration)
            {
                info(
                    "BMC configuration detected, will create a corresponding Inventory item");
                healthMon->createBmcInventoryIfNotCreated();
                needUpdate = true;
            }
        });

    // Start the timer
    io.post(
        [conn]() { sensorRecreateTimerCallback(sensorRecreateTimer, *conn); });
    io.run();

    return 0;
}
