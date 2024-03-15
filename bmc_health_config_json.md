# bmc_health_config.json

This file outlines the format for the supplemental health metric config that can
be supplied by the platform to override the default in-code config from
health_metric_config.cpp.

The json config may have following metric types -

- `CPU`
  - This indicates the total CPU utilization.
- `CPU_User`
  - This indicates the user level CPU utilization.
- `CPU_Kernel`
  - This indicates the kernel level CPU utilization.
- `Memory`
  - This indicates the total memory for the system, which is a constant metric
    and doesn't change.
- `Memory_Available`
  - This indicates the memory which is not used, in addition to buffered &
    cached memory that can be made available by OS depending on need.
- `Memory_Free`
  - This indicates the amount of un-used memory.
- `Memory_Shared`
  - This indicates the amount of memory being shared between processes within
    the system.
- `Memory_Buffered_And_Cached`
  - This indicates the amount of memory being used for caching and temporary
    buffers.
- `Storage_RW`
  - This indicates the amount of available storage space
- `Storage_`\<xxx>
  - This indicates the amount of availble space for type depicted by `<xxx>` for
    the location backed by path parameter.

The metric types may have the following attributes:

- `Window_size`
  - This indicates the number of samples being used for threshold value
    computations.
- `Path`
  - The path attribute is applicable to storage metrics and indicates the
    directory path for it.
- `Hysteresis`
  - This indicates the percentage beyond which the metric value change (since
    last notified) should be reported as a D-Bus signal.
- `Threshold`
  - The following threshold levels (with bounds) are supported.
    - `HardShutdown_Lower`
    - `HardShutdown_Upper`
    - `SoftShutdown_Lower`
    - `SoftShutdown_Upper`
    - `PerformanceLoss_Lower`
    - `PerformanceLoss_Upper`
    - `Critical_Lower`
    - `Critical_Upper`
    - `Warning_Lower`
    - `Warning_Upper`
  - Threshold may have following attributes
    - `Value`
      - This indicates the percentage value at which specific threshold gets
        asserted.
        - For lower bound, the threshold gets asserted if metric value falls
          below the specified threshold percentage value.
        - For upper bound, the threshold gets asserted if metric value goes
          beyond the specified threshold percentage value.
    - `Log` -A boolean value of true/false depicts if a critical system message
      shall be logged when threshold gets asserted.
    - `Target`
      - This indicates the systemd target which shall be run when the specific
        threshold gets asserted.

Example:

```json
    "CPU": {
        "Window_size": 120,
        "Hysteresis": 1.0,
        "Threshold": {
            "Critical_Upper": {
                "Value": 90.0,
                "Log": true,
                "Target": ""
            },
            "Warning_Upper": {
                "Value": 80.0,
                "Log": false,
                "Target": ""
            }
        }
    }
```
