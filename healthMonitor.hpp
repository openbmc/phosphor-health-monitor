#include <nlohmann/json.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/server.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/server.hpp>
#include <xyz/openbmc_project/Sensor/Value/server.hpp>

#include <deque>
#include <map>
#include <string>

namespace phosphor
{
namespace health
{

using Json = nlohmann::json;
using ValueIface = sdbusplus::xyz::openbmc_project::Sensor::server::Value;

using CriticalInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Critical;

using WarningInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Warning;

using healthIfaces =
    sdbusplus::server::object::object<ValueIface, CriticalInterface,
                                      WarningInterface>;

using Association = std::tuple<std::string, std::string, std::string>;

struct HealthConfig
{
    std::string name;
    uint16_t freq;
    uint16_t windowSize;
    double criticalHigh;
    double warningHigh;
    bool criticalLog;
    bool warningLog;
    std::string criticalTgt;
    std::string warningTgt;
    std::string path;
};

class HealthSensor : public healthIfaces
{
  public:
    HealthSensor() = delete;
    HealthSensor(const HealthSensor&) = delete;
    HealthSensor& operator=(const HealthSensor&) = delete;
    HealthSensor(HealthSensor&&) = delete;
    HealthSensor& operator=(HealthSensor&&) = delete;
    virtual ~HealthSensor() = default;

    /** @brief Constructs HealthSensor
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] objPath - The Dbus path of health sensor
     */
    HealthSensor(sdbusplus::bus::bus& bus,
                 sdbusplus::asio::object_server& objectServer,
                 const char* objPath, HealthConfig& sensorConfig) :
        healthIfaces(bus, objPath),
        bus(bus), objectServer(objectServer), sensorConfig(sensorConfig),
        timerEvent(sdeventplus::Event::get_default()),
        readTimer(timerEvent, std::bind(&HealthSensor::readHealthSensor, this))
    {
        initHealthSensor();
        association = objectServer.add_interface(
            objPath, "xyz.openbmc_project.Association.Definitions");
        itemAssoc = objectServer.add_interface(
            objPath, "xyz.openbmc_project.Association.Definitions");

        const std::string boardPath =
            "/xyz/openbmc_project/inventory/system/chassis";

        itemAssoc->register_property("Associations",
                                     std::vector<Association>{
                                         {"chassis", "all_sensors", boardPath},
                                         {"inventory", "sensors", boardPath},
                                     });
        itemAssoc->initialize();
    }

    /** @brief list of sensor data values */
    std::deque<double> valQueue;

    void initHealthSensor();
    /** @brief Set sensor value utilization to health sensor D-bus  */
    void setSensorValueToDbus(const double value);
    /** @brief Set Sensor Threshold to D-bus at beginning */
    void setSensorThreshold(double criticalHigh, double warningHigh);
    /** @brief Check Sensor threshold and update alarm and log */
    void checkSensorThreshold(const double value);

  private:
    /** @brief sdbusplus bus client connection. */
    sdbusplus::bus::bus& bus;
    sdbusplus::asio::object_server& objectServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> itemAssoc;

    /** @brief Sensor config from config file */
    HealthConfig& sensorConfig;
    /** @brief the Event Loop structure */
    sdeventplus::Event timerEvent;
    /** @brief Sensor Read Timer */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> readTimer;

    std::shared_ptr<sdbusplus::asio::dbus_interface> association;
    /** @brief Read sensor at regular intrval */
    void readHealthSensor();
};

class HealthMon
{
  public:
    HealthMon() = delete;
    HealthMon(const HealthMon&) = delete;
    HealthMon& operator=(const HealthMon&) = delete;
    HealthMon(HealthMon&&) = delete;
    HealthMon& operator=(HealthMon&&) = delete;
    virtual ~HealthMon() = default;

    /** @brief Constructs HealthMon
     *
     * @param[in] bus     - Handle to system dbus
     */
    HealthMon(sdbusplus::bus::bus& bus,
              sdbusplus::asio::object_server& objectServer) :
        bus(bus),
        objectServer(objectServer)
    {
        // read json file
        sensorConfigs = getHealthConfig();
        createHealthSensors();
    }

    /** @brief Parsing Health config JSON file  */
    Json parseConfigFile(std::string configFile);

    /** @brief reading config for each health sensor component */
    void getConfigData(Json& data, HealthConfig& cfg);

    /** @brief Map of the object HealthSensor */
    std::unordered_map<std::string, std::shared_ptr<HealthSensor>>
        healthSensors;

    /** @brief Create sensors for health monitoring */
    void createHealthSensors();

  private:
    sdbusplus::bus::bus& bus;
    sdbusplus::asio::object_server& objectServer;
    std::vector<HealthConfig> sensorConfigs;
    std::vector<HealthConfig> getHealthConfig();
};

} // namespace health
} // namespace phosphor
