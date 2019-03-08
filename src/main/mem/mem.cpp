#include <iostream>
#include <stdio.h>
#include <numa.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <numaif.h>

#include "mem.h"

using namespace coypu::mem;

int MemManager::SetPolicyBind (int node) {
    unsigned long mask = 0;
    mask |= (mask << node);
    return ::set_mempolicy(MPOL_BIND, &mask, GetMaxNumaNode());
}

long MemManager::GetPageSize() 
 {
     return ::sysconf(_SC_PAGESIZE);
 }

int MemManager::GetMaxNumaNode() 
{
    return ::numa_max_node();
}

int CPUManager::RunOnNode(int node) {
    return ::numa_run_on_node(node);
}

void *MemManager::AllocOnNode (int node, size_t size) {
    if (node > ::numa_max_node()) return nullptr;

    return ::numa_alloc_onnode(node, size);
}

int MemManager::GetCPUCount () {
    return ::numa_num_configured_cpus();
}

int CPUManager::SetName (const std::string &name) {
    return ::pthread_setname_np(pthread_self(), name.c_str());
}

int CPUManager::SetCPU (int cpu) 
{
    int cpuCount = MemManager::GetCPUCount();
    if (cpu >= cpuCount) return -1;
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    return ::sched_setaffinity(0, sizeof(cpu_set_t), &set);
}

int CPUManager::SetCPU (cpu_set_t &set) 
{
    return ::sched_setaffinity(0, sizeof(cpu_set_t), &set);
}

int CPUManager::SetCPUs (const std::string &cpuStr) 
{
    cpu_set_t set;

    CPU_ZERO(&set);
    if (!ParseCPUs(cpuStr, set)) return -2;

    return ::sched_setaffinity(0, sizeof(cpu_set_t), &set);
}


bool CPUManager::ParseCPUs (const std::string &cpuStr, cpu_set_t &set) {
  // use numa_parse_cpustring_all - supports isolated cpus
    struct bitmask *cpus = ::numa_parse_cpustring_all(cpuStr.c_str());

    if (cpus) {
        int cpuCount = MemManager::GetCPUCount();
        for (int i = 0; i < cpuCount; ++i) {
            if (::numa_bitmask_isbitset(cpus, i)) {
                CPU_SET(i, &set);
            }
        }
        ::numa_bitmask_free(cpus);
        return true;
    }
    return false;
}

void MemManager::ToNode (void *mem, size_t size, int node) {
    numa_tonode_memory(mem, size, node);
}
