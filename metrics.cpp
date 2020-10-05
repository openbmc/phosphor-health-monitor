#include <sys/sysinfo.h>

#include <phosphor-logging/log.hpp>

#include <fstream>
#include <iostream>
#include <sstream>

extern bool DEBUG;
using namespace phosphor::logging;

enum CPUStatesTime
{
    USER_IDX = 0,
    NICE_IDX,
    SYSTEM_IDX,
    IDLE_IDX,
    IOWAIT_IDX,
    IRQ_IDX,
    SOFTIRQ_IDX,
    STEAL_IDX,
    GUEST_USER_IDX,
    GUEST_NICE_IDX,
    NUM_CPU_STATES_TIME
};

double readCPUUtilization()
{
    std::ifstream fileStat("/proc/stat");
    if (!fileStat.is_open())
    {
        log<level::ERR>("cpu file not available",
                        entry("FILENAME = /proc/stat"));
        return -1;
    }

    std::string firstLine, labelName;
    std::size_t timeData[NUM_CPU_STATES_TIME];

    std::getline(fileStat, firstLine);
    std::stringstream ss(firstLine);
    ss >> labelName;

    if (DEBUG)
        std::cout << "CPU stats first Line is " << firstLine << "\n";

    if (labelName.compare("cpu"))
    {
        log<level::ERR>("CPU data not available");
        return -1;
    }

    int i;
    for (i = 0; i < NUM_CPU_STATES_TIME; i++)
    {
        if (!(ss >> timeData[i]))
            break;
    }

    if (i != NUM_CPU_STATES_TIME)
    {
        log<level::ERR>("CPU data not correct");
        return -1;
    }

    static double preActiveTime = 0, preIdleTime = 0;
    double activeTime, activeTimeDiff, idleTime, idleTimeDiff, totalTime,
        activePercValue;

    idleTime = timeData[IDLE_IDX] + timeData[IOWAIT_IDX];
    activeTime = timeData[USER_IDX] + timeData[NICE_IDX] +
                 timeData[SYSTEM_IDX] + timeData[IRQ_IDX] +
                 timeData[SOFTIRQ_IDX] + timeData[STEAL_IDX] +
                 timeData[GUEST_USER_IDX] + timeData[GUEST_NICE_IDX];

    idleTimeDiff = idleTime - preIdleTime;
    activeTimeDiff = activeTime - preActiveTime;

    /* Store current idle and active time for next calculation */
    preIdleTime = idleTime;
    preActiveTime = activeTime;

    totalTime = idleTimeDiff + activeTimeDiff;

    activePercValue = activeTimeDiff / totalTime * 100;

    if (DEBUG)
        std::cout << "CPU Utilization is " << activePercValue << "\n";

    return activePercValue;
}

double readMemoryUtilization()
{
    struct sysinfo s_info;

    sysinfo(&s_info);
    double usedRam = s_info.totalram - s_info.freeram;
    double memUsePerc = usedRam / s_info.totalram * 100;

    if (DEBUG)
    {
        std::cout << "Memory Utilization is " << memUsePerc << "\n";

        std::cout << "TotalRam: " << s_info.totalram
                  << " FreeRam: " << s_info.freeram << "\n";
        std::cout << "UseRam: " << usedRam << "\n";
    }

    return memUsePerc;
}