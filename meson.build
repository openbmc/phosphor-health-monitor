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
    nlohmann_json = declare_dependency(
        include_directories: [
            'subprojects/nlohmann-json/single_include',
            'subprojects/nlohmann-json/single_include/nlohmann',
        ]
    )
endif

executable(
    'health-monitor',
    [
        'healthMonitor.cpp',
    ],
    dependencies: [
        dependency(
            'phosphor-dbus-interfaces',
            fallback: [
                'phosphor-dbus-interfaces',
                'phosphor_dbus_interfaces_dep',
            ],
        ),
        dependency(
            'phosphor-logging',
            fallback: [ 'phosphor-logging', 'phosphor_logging_dep' ],
        ),
        dependency(
            'sdbusplus',
            fallback: [ 'sdbusplus', 'sdbusplus_dep' ],
        ),
        dependency(
            'sdeventplus',
            fallback: [ 'sdeventplus', 'sdeventplus_dep' ],
        ),
        nlohmann_json,
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
