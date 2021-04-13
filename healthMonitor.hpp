#include <nlohmann/json.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
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

using namespace phosphor::logging;

using Json = nlohmann::json;
using ValueIface = sdbusplus::xyz::openbmc_project::Sensor::server::Value;

using CriticalInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Critical;

using WarningInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Warning;

using AssociationDefinitionInterface =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;

using healthIfaces =
    sdbusplus::server::object::object<ValueIface, CriticalInterface,
                                      WarningInterface,
                                      AssociationDefinitionInterface>;

using AssociationTuple = std::tuple<std::string, std::string, std::string>;

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
    HealthSensor(sdbusplus::bus::bus& bus, const char* objPath,
                 HealthConfig& sensorConfig,
                 const std::vector<std::string>& chassisIds) :
        healthIfaces(bus, objPath),
        bus(bus), sensorConfig(sensorConfig),
        timerEvent(sdeventplus::Event::get_default()),
        readTimer(timerEvent, std::bind(&HealthSensor::readHealthSensor, this))
    {
        initHealthSensor(chassisIds);
    }

    /** @brief list of sensor data values */
    std::deque<double> valQueue;
    /** @brief Initialize sensor, set default value and association */
    void initHealthSensor(const std::vector<std::string>& chassisIds);
    /** @brief Set sensor value utilization to health sensor D-bus  */
    void setSensorValueToDbus(const double value);
    /** @brief Set Sensor Threshold to D-bus at beginning */
    void setSensorThreshold(double criticalHigh, double warningHigh);
    /** @brief Check Sensor threshold and update alarm and log */
    void checkSensorThreshold(const double value);

  private:
    /** @brief sdbusplus bus client connection. */
    sdbusplus::bus::bus& bus;
    /** @brief Sensor config from config file */
    HealthConfig& sensorConfig;
    /** @brief the Event Loop structure */
    sdeventplus::Event timerEvent;
    /** @brief Sensor Read Timer */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> readTimer;
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
    HealthMon(sdbusplus::bus::bus& bus) : bus(bus)
    {
        // Find all chassis in the system
        sdbusplus::message::message msg = bus.new_method_call(
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths");
        // Signature of GetSubTreePaths: sias
        msg.append("/xyz/openbmc_project/inventory/system");
        msg.append(2);

        // FIXME: Dbus-sensors use "xyz.openbmc_project.Inventory.Item.System"
        // here but I can't seem to find any chassis using this parameter.
        // I had to use "xyz.openbmc_project.Inventory.Item.Chassis".
        msg.append(std::vector<std::string>{
            "xyz.openbmc_project.Inventory.Item.Chassis"});
        sdbusplus::message::message reply = bus.call(msg, 0);
        std::vector<std::string> chassisIds;
        if (!strcmp(reply.get_signature(), "as"))
        {
            reply.read(chassisIds);
            log<level::INFO>(std::string("Found " +
                                          std::to_string(chassisIds.size()) +
                                          " chassis:")
                                 .c_str());
            for (unsigned i = 0; i < chassisIds.size(); i++)
            {
                log<level::INFO>(std::string("Chassis " + std::to_string(i) +
                                             ": " + chassisIds[i])
                                      .c_str());
            }
        }
        else
        {
            log<level::WARNING>(
                "Did not find any chassis, cannot create association");
        }

        // Read JSON file
        sensorConfigs = getHealthConfig();

        // Create health sensors
        createHealthSensors(chassisIds);
    }

    /** @brief Parse Health config JSON file  */
    Json parseConfigFile(std::string configFile);

    /** @brief Read config for each health sensor component */
    void getConfigData(Json& data, HealthConfig& cfg);

    /** @brief Map of the object HealthSensor */
    std::unordered_map<std::string, std::shared_ptr<HealthSensor>>
        healthSensors;

    /** @brief Create sensors for health monitoring */
    void createHealthSensors(const std::vector<std::string>& chassisIds);

  private:
    sdbusplus::bus::bus& bus;
    std::vector<HealthConfig> sensorConfigs;
    std::vector<HealthConfig> getHealthConfig();
};

} // namespace health
} // namespace phosphor
