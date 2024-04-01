gtest_dep = dependency('gtest', main: true, disabler: true, required: false)
gmock_dep = dependency('gmock', disabler: true, required: false)
if not gtest_dep.found() or not gmock_dep.found()
    gtest_proj = import('cmake').subproject('googletest', required: false)
    if gtest_proj.found()
        gtest_dep = declare_dependency(
            dependencies: [
                dependency('threads'),
                gtest_proj.dependency('gtest'),
                gtest_proj.dependency('gtest_main'),
            ]
        )
        gmock_dep = gtest_proj.dependency('gmock')
    else
        assert(
            not get_option('tests').enabled(),
            'Googletest is required if tests are enabled'
        )
    endif
endif

test(
    'test_health_metric_config',
    executable(
        'test_health_metric_config',
        'test_health_metric_config.cpp',
        '../health_metric_config.cpp',
        dependencies: [
            gtest_dep,
            gmock_dep,
            phosphor_logging_dep,
            phosphor_dbus_interfaces_dep,
            sdbusplus_dep,
            nlohmann_json_dep
        ],
        include_directories: '../',
    )
)

test(
    'test_health_metric',
    executable(
        'test_health_metric',
        'test_health_metric.cpp',
        '../health_metric.cpp',
        '../health_utils.cpp',
        '../health_metric_config.cpp',
        dependencies: [
            gtest_dep,
            gmock_dep,
            phosphor_logging_dep,
            phosphor_dbus_interfaces_dep,
            sdbusplus_dep,
            nlohmann_json_dep
        ],
        include_directories: '../',
    )
)

test(
    'test_health_metric_collection',
    executable(
        'test_health_metric_collection',
        'test_health_metric_collection.cpp',
        '../health_metric_collection.cpp',
        '../health_metric.cpp',
        '../health_metric_config.cpp',
        '../health_utils.cpp',
        dependencies: [
            gtest_dep,
            gmock_dep,
            phosphor_logging_dep,
            phosphor_dbus_interfaces_dep,
            sdbusplus_dep,
            nlohmann_json_dep
        ],
        include_directories: '../',
    )
)
