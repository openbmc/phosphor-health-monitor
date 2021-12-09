project(
    'phosphor-health-monitor',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++20',
    ],
    meson_version: '>=0.57.0',
)

cpp = meson.get_compiler('cpp')

if cpp.has_header('nlohmann/json.hpp')
    nlohmann_json = declare_dependency()
else
    subproject('nlohmann-json')
    nlohmann_json = dependency('nlohmann_json')
endif

sdbusplus_dep = dependency('sdbusplus', required: false)
if sdbusplus_dep.found() and sdbusplus_dep.type_name() != 'internal'
  sdbusplusplus_prog = find_program('sdbus++', native: true)
else
  sdbusplus_proj = subproject('sdbusplus', required: true)
  sdbusplus_dep = sdbusplus_proj.get_variable('sdbusplus_dep')
  sdbusplusplus_prog = sdbusplus_proj.get_variable('sdbusplusplus_prog')
endif

phosphor_health_monitor_root = meson.current_source_dir()
phosphor_health_monitor_headers = include_directories('.')

subdir('xyz/openbmc_project/I2C/I2CStats')

executable(
    'health-monitor',
    [
        'healthMonitor.cpp',
        'i2cstats.cpp',
        'i2ctopology.cpp',
    ],
    dependencies: [
        dependency('phosphor-dbus-interfaces'),
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('sdeventplus'),
        nlohmann_json,
        i2cstats_dbus_dep
    ],
    install: true,
    install_dir: get_option('bindir')
)

install_data(sources : 'bmc_health_config.json', install_dir : '/etc/healthMon')

conf_data = configuration_data()
conf_data.set('HEALTH_CONFIG_FILE', '"/etc/healthMon/bmc_health_config.json"')
conf_data.set('HEALTH_BUS_NAME', '"xyz.openbmc_project.HealthMon"')
conf_data.set('HEALTH_SENSOR_PATH', '"/xyz/openbmc_project/sensors/utilization/"')
conf_data.set('SENSOR_OBJPATH', '"/xyz/openbmc_project/sensors"')
conf_data.set('INVENTORY_OBJPATH', '"/xyz/openbmc_project/inventory"')

configure_file(output : 'config.h',
               configuration : conf_data)

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
  input: 'phosphor-health-monitor.service.in',
  output: 'phosphor-health-monitor.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir'))
