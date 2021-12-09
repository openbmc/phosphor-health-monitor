#pragma once

#include <phosphor-logging/log.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace phosphor::logging;

class I2CTopologyMap
{
  public:
    struct I2CNode
    {
        int parentIdx;
        std::set<int> children;
        I2CNode() : parentIdx(-1)
        {
            parentIdx = -1;
        }
    };
    std::vector<I2CNode*> nodes;
    I2CTopologyMap(int n);
    I2CTopologyMap();
    I2CNode* GetNodeByIndex(int idx);
    void AddEdge(int parent, int child);

    // Extracts the I2C nodes that lead to an HwMon
    void ReadHwmonPath(const std::string& path);

  private:
    // Example: /sys/class/i2c-dev/i2c-0/
    void do_TraverseI2C(const std::string& d,
                        std::vector<int>* parents, // the path taken
                        std::vector<std::vector<int>>*
                            parents1 // includes all devices under the node
    );

  public:
    // Traverse "/sys/class/i2c-dev" for I2C topology
    void TraverseI2C()
    {
        std::vector<int> par = {-1};
        std::vector<std::vector<int>> par1;
        do_TraverseI2C("/sys/class/i2c-dev", &par, &par1);
        rootBuses = FindRootBuses();
    }

    std::vector<std::pair<int, std::string>> GetRootBusesAndAPBAddresses()
    {
        std::vector<std::pair<int, std::string>> ret;
        for (int busId : rootBuses)
        {
            ret.push_back({busId, GetAPBAddress(busId)});
        }
        return ret;
    }

    std::string GetAPBAddress(int busId)
    {
        if (apbAddresses.count(busId) == 0)
        {
            return "";
        }
        return apbAddresses[busId];
    }

  public:
    void LoadDummyData();

  private:
    std::vector<int> rootBuses;
    std::unordered_map<int, std::string> apbAddresses;
    // Traverse "/sys/devices/platform/ahb/ahb:apb" for a list of physical
    // I2C buses.
    // The results from TraverseI2C() and FindRootBuses() should be the same.
    std::vector<int> FindRootBuses();
};