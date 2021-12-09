#include "i2ctopology.hpp"

#include <phosphor-logging/log.hpp>

namespace
{
std::string ExtractRelativePath(const std::string& s)
{
    std::string::size_type idx = s.rfind("/");
    if (idx == std::string::npos)
        return s;
    else
        return s.substr(idx + 1);
}
} // namespace

I2CTopologyMap::I2CTopologyMap(int n)
{
    nodes.resize(n);
}

I2CTopologyMap::I2CTopologyMap()
{
    nodes.resize(256);
}

I2CTopologyMap::I2CNode* I2CTopologyMap::GetNodeByIndex(int idx)
{
    if (nodes[idx] == nullptr)
    {
        nodes[idx] = new I2CNode();
    }
    return nodes[idx];
}

void I2CTopologyMap::AddEdge(int parent, int child)
{
    if (parent != -1)
    {
        I2CNode *p = GetNodeByIndex(parent), *c = GetNodeByIndex(child);
        p->children.insert(child);
        c->parentIdx = parent;
    }
    else
    { // add root node
        GetNodeByIndex(child);
    }
}

void I2CTopologyMap::ReadHwmonPath(const std::string& path)
{
    std::string::size_type idx = 0, idx_next;
    std::vector<int> i2c_ids;
    while (true)
    {
        idx_next = path.find("/", idx);
        if (idx_next == std::string::npos)
        {
            break;
        }
        const std::string& chunk = path.substr(idx, idx_next - idx);
        if (chunk.find("i2c-") == 0)
        {
            int i2c_id = std::atoi(chunk.substr(4).c_str());
            i2c_ids.push_back(i2c_id);
        }
        idx = idx_next + 1;
    }

    for (size_t i = 0; i + 1 < i2c_ids.size(); i++)
    {
        AddEdge(i2c_ids[i], i2c_ids[i + 1]);
    }
}

void I2CTopologyMap::do_TraverseI2C(
    const std::string& d,
    std::vector<int>* parents, // the path taken
    std::vector<std::vector<int>>*
        parents1 // includes all devices under the node
)
{
    std::string d1 = d;
    if (parents->size() > 1)
    {
        d1 += "/device";
    }

    bool has_self = false;
    std::vector<int> children;
    std::vector<std::string> children_paths;

    if (std::filesystem::exists(d1))
    {
        for (const auto& entry : std::filesystem::directory_iterator(d1))
        {
            const std::string& path = entry.path();
            const std::string& relpath = ExtractRelativePath(path);
            if (relpath.find("i2c-") == 0)
            {
                std::string i2c_id;
                for (int i = 4; i < int(relpath.size()); i++)
                {
                    const char c = relpath[i];
                    if (c >= '0' && c <= '9')
                    {
                        i2c_id.push_back(c);
                    }
                }
                int x = std::atoi(i2c_id.c_str());
                bool found = false;
                for (const std::vector<int>& yy : *parents1)
                {
                    for (const int y : yy)
                    {
                        if (y == x)
                        {
                            found = true;
                        }
                    }
                }
                if (!found && !i2c_id.empty())
                {
                    children.push_back(x);
                    children_paths.push_back(path);
                    if (x == parents->back())
                    {
                        has_self = true;
                    }
                }
            }
        }
    }

    bool is_root = (parents->size() == 1);

    if (!has_self)
    {
        if (!is_root)
        {
            parents1->push_back(children);
        }
        for (int i = 0; i < int(children.size()); i++)
        {
            AddEdge(parents->back(), children[i]);
            parents->push_back(children[i]);
            do_TraverseI2C(children_paths[i], parents, parents1);
            parents->pop_back();
        }
        if (!is_root)
        {
            parents1->pop_back();
        }
    }
}

std::vector<int> I2CTopologyMap::FindRootBuses()
{
    std::vector<int> ret;
    std::string ahb_apb_path = "/sys/devices/platform/ahb/ahb:apb/";
    if (!std::filesystem::exists(ahb_apb_path))
    {
        // log error
        log<level::ERR>(("Error: Could not find " + ahb_apb_path).c_str());
        return ret;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(ahb_apb_path))
    {
        const std::string& path = entry.path();
        printf(">> %s\n", path.c_str());
        if (path.find(".i2c") == path.size() - 4)
        {
            int num_i2c_devices = 0;
            int first_i2c_idx = -999;
            for (const std::filesystem::directory_entry& entry1 :
                 std::filesystem::directory_iterator(path))
            {
                const std::string path1 = entry1.path();
                printf("  >> %s\n", path1.c_str());
                const std::string file_path = ExtractRelativePath(path1);
                if (file_path.find("i2c-") == 0)
                {
                    if (num_i2c_devices == 0)
                    {
                        first_i2c_idx = std::atoi(file_path.c_str() + 4);
                    }
                    num_i2c_devices++;
                }
            }
            if (num_i2c_devices == 1)
            {
                rootBuses.push_back(first_i2c_idx);
                // Example value: "f0082000.i2c"
                const std::string file_path = ExtractRelativePath(path);
                apbAddresses[first_i2c_idx] =
                    file_path.substr(0, file_path.size() - 4);
                ret.push_back(first_i2c_idx);
            }
        }
    }
    return ret;
}