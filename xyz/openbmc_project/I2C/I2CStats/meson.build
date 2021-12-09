domain = 'xyz.openbmc_project.I2C.I2CStats'
if_yaml_file = files('../I2CStats.interface.yaml')

if_cpp = custom_target(
  'server.cpp',
  output: 'server.cpp',
  input: if_yaml_file,
  capture: true,
  command: [sdbusplusplus_prog, '-r', phosphor_health_monitor_root, 'interface', 'server-cpp', domain])

if_hpp = custom_target(
  'server.hpp',
  output: 'server.hpp',
  input: if_yaml_file,
  capture: true,
  command: [sdbusplusplus_prog, '-r', phosphor_health_monitor_root, 'interface', 'server-header', domain],
  install: true,
  install_dir: get_option('includedir') / 'xyz/openbmc_project/I2C/I2CStats')

i2cstats_dbus_deps = [
  dependency(
    'phosphor-dbus-interfaces',
    fallback: ['phosphor-dbus-interfaces', 'phosphor_dbus_interfaces_dep']),
  sdbusplus_dep
]

i2cstats_dbus_lib = library(
  'i2cstats-dbus',
  [
    if_cpp,
    if_hpp,
  ],
  implicit_include_directories: false,
  include_directories: phosphor_health_monitor_headers,
  version: meson.project_version(),
  dependencies: i2cstats_dbus_deps,
  install: true)

i2cstats_dbus_dep = declare_dependency(
  dependencies: i2cstats_dbus_deps,
  sources: [if_hpp],
  link_with: i2cstats_dbus_lib)

i2cstats_dbus_reqs = []
foreach dep : i2cstats_dbus_deps
  if dep.type_name() == 'pkgconfig'
    i2cstats_dbus_reqs += dep
  endif
endforeach

import('pkgconfig').generate(
  i2cstats_dbus_lib,
  description: 'I2CStats DBus Bindings',
  version: meson.project_version(),
  requires: i2cstats_dbus_reqs)
