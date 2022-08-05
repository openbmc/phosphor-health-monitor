#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/server.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/server.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/server.hpp>
#include <xyz/openbmc_project/Sensor/Value/server.hpp>

constexpr char BMC_INVENTORY_ITEM[] = "xyz.openbmc_project.Inventory.Item.Bmc";
constexpr char BMC_CONFIGURATION[] = "xyz.openbmc_project.Configuration.Bmc";

#include <deque>
#include <limits>
#include <map>
#include <string>

namespace phosphor
{
namespace health
{

const char* InventoryPath = "/xyz/openbmc_project/inventory";

// Used for identifying the BMC inventory creation signal
const char* BMCActivationPath = "/xyz/openbmc_project/inventory/bmc/activation";

bool FindSystemInventoryInObjectMapper(sdbusplus::bus_t& bus)
{
    sdbusplus::message_t msg =
        bus.new_method_call("xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetObject");
    msg.append(InventoryPath);
    msg.append(std::vector<std::string>{});

    try
    {
        sdbusplus::message_t reply = bus.call(msg, 0);
        return true;
    }
    catch (const std::exception& e)
    {}
    return false;
}

using Json = nlohmann::json;
using ValueIface = sdbusplus::xyz::openbmc_project::Sensor::server::Value;

using CriticalInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Critical;

using WarningInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Warning;

using AssociationDefinitionInterface =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;

using healthIfaces =
    sdbusplus::server::object_t<ValueIface, CriticalInterface, WarningInterface,
                                AssociationDefinitionInterface>;

using BmcInterface = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Bmc>;

using AssociationTuple = std::tuple<std::string, std::string, std::string>;

struct HealthConfig
{
    std::string name;
    uint16_t freq;
    uint16_t windowSize;
    double criticalHigh = std::numeric_limits<double>::quiet_NaN();
    double warningHigh = std::numeric_limits<double>::quiet_NaN();
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
    HealthSensor(sdbusplus::bus_t& bus, const char* objPath,
                 HealthConfig& sensorConfig,
                 const std::vector<std::string>& bmcIds) :
        healthIfaces(bus, objPath),
        bus(bus), sensorConfig(sensorConfig),
        timerEvent(sdeventplus::Event::get_default()),
        readTimer(timerEvent, std::bind(&HealthSensor::readHealthSensor, this))
    {
        initHealthSensor(bmcIds);
    }

    /** @brief list of sensor data values */
    std::deque<double> valQueue;
    /** @brief Initialize sensor, set default value and association */
    void initHealthSensor(const std::vector<std::string>& bmcIds);
    /** @brief Set sensor value utilization to health sensor D-bus  */
    void setSensorValueToDbus(const double value);
    /** @brief Set Sensor Threshold to D-bus at beginning */
    void setSensorThreshold(double criticalHigh, double warningHigh);
    /** @brief Check Sensor threshold and update alarm and log */
    void checkSensorThreshold(const double value);

  private:
    /** @brief sdbusplus bus client connection. */
    sdbusplus::bus_t& bus;
    /** @brief Sensor config from config file */
    HealthConfig& sensorConfig;
    /** @brief the Event Loop structure */
    sdeventplus::Event timerEvent;
    /** @brief Sensor Read Timer */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> readTimer;
    /** @brief Read sensor at regular intrval */
    void readHealthSensor();
    /** @brief Start configured threshold systemd unit */
    void startUnit(const std::string& sysdUnit);
};

class BmcInventory : public BmcInterface
{
  public:
    BmcInventory() = delete;
    BmcInventory(sdbusplus::bus::bus& bus, const char* objPath) :
        BmcInterface(bus, objPath)
    {}
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

    /** @brief Recreates sensor objects and their association if possible
     */
    void recreateSensors();

    /** @brief Constructs HealthMon
     *
     * @param[in] bus     - Handle to system dbus
     */
    HealthMon(sdbusplus::bus_t& bus) : bus(bus)
    {
        // Read JSON file
        sensorConfigs = getHealthConfig();
        recreateSensors();
    }

    /** @brief Parse Health config JSON file  */
    Json parseConfigFile(std::string configFile);

    /** @brief Read config for each health sensor component */
    void getConfigData(Json& data, HealthConfig& cfg);

    /** @brief Map of the object HealthSensor */
    std::unordered_map<std::string, std::shared_ptr<HealthSensor>>
        healthSensors;

    /** @brief Create sensors for health monitoring */
    void createHealthSensors(const std::vector<std::string>& bmcIds);

    /** @brief Create the BMC Inventory object */
    void createBmcInventoryIfNotCreated();

    std::shared_ptr<BmcInventory> bmcInventory;

    bool bmcInventoryCreated();

  private:
    sdbusplus::bus_t& bus;
    std::vector<HealthConfig> sensorConfigs;
    std::vector<HealthConfig> getHealthConfig();
};

} // namespace health
} // namespace phosphor
