project(
    'phosphor-health-monitor',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++17',
    ],
)

protoc = find_program('protoc', required: false)
proto_dep = dependency('protobuf', required: false).partial_dependency(compile_args : false,
  link_args : true, links : true, includes : true)

executable(
    'health-monitor',
    [
        'healthMonitor.cpp',
    ],
    dependencies: [
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('phosphor-dbus-interfaces'),
        dependency('sdeventplus'),
    ],
    install: true,
    install_dir: get_option('bindir')
)

# IPMI blob handler that accompanies the daemon
shared_library(
  'metrics',
  [
    'ipmi/main.cpp',
    'ipmi/handler.cpp',
    'ipmi/metric.cpp',
  ],
  install: true,
  install_dir: '/usr/lib/blob-ipmid/',
  dependencies:[
    dependency('phosphor-logging'),
    dependency('protobuf'),
    dependency('phosphor-ipmi-blobs'),
  ],
  version: '0'
)

install_data(sources : 'bmc_health_config.json', install_dir : '/etc/healthMon')

conf_data = configuration_data()
conf_data.set('HEALTH_CONFIG_FILE', '"/etc/healthMon/bmc_health_config.json"')
conf_data.set('HEALTH_BUS_NAME', '"xyz.openbmc_project.HealthMon"')
conf_data.set('HEALTH_SENSOR_PATH', '"/xyz/openbmc_project/sensors/utilization/"')
conf_data.set('SENSOR_OBJPATH', '"/xyz/openbmc_project/sensors"')

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
