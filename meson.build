project(
    'phosphor-health-monitor',
    'cpp',
    version: '1.0',
    default_options: ['cpp_std=c++23'],
    meson_version: '>=1.1.1',
)

phosphor_logging_dep = dependency('phosphor-logging')
phosphor_dbus_interfaces_dep = dependency('phosphor-dbus-interfaces')
sdbusplus_dep = dependency('sdbusplus')
sdeventplus_dep = dependency('sdeventplus')
nlohmann_json_dep = dependency('nlohmann_json', include_type: 'system')
base_deps = [
    phosphor_logging_dep,
    phosphor_dbus_interfaces_dep,
    sdbusplus_dep,
    sdeventplus_dep,
    nlohmann_json_dep,
]

executable(
    'health-monitor',
    [
        'health_metric_config.cpp',
        'health_metric.cpp',
        'health_utils.cpp',
        'health_metric_collection.cpp',
        'health_monitor.cpp',
    ],
    dependencies: [base_deps],
    install: true,
    install_dir: get_option('bindir'),
)

conf_data = configuration_data()
conf_data.set('HEALTH_CONFIG_FILE', '"/etc/healthMon/bmc_health_config.json"')
conf_data.set(
    'MONITOR_COLLECTION_INTERVAL',
    get_option('monitor-collection-interval'),
)

configure_file(output: 'config.h', configuration: conf_data)

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
    input: 'phosphor-health-monitor.service.in',
    output: 'phosphor-health-monitor.service',
    configuration: conf_data,
    install: true,
    install_dir: systemd.get_variable('systemdsystemunitdir'),
)

if get_option('tests').allowed()
    subdir('test')
endif
