#ifndef __COYPU_MEM_H
#define __COYPU_MEM_H

#include <sched.h>
#include <string>
namespace coypu {
    namespace mem {
        class CPUManager {
            public:
                static int SetCPU (int cpu);
                static int SetCPU (cpu_set_t &set);
                static int SetCPUs (const std::string &cpuStr);

                static int RunOnNode(int node);

                // Use numa cpu string but sets hwloc version (man numa)
                static bool ParseCPUs (const std::string &cpus, cpu_set_t &set);

                static int SetName (const std::string &name);

            private:
                CPUManager() = delete;
        };
        class MemManager {
        public:
            static int GetMaxNumaNode();
            static int GetCPUCount ();
            static long GetPageSize();
            static int SetPolicyBind (int node);

            static void *AllocOnNode (int node, size_t size);
            static void ToNode (void *mem, size_t size, int node);

        private:
            MemManager() = delete;
        };
    }
}

#endif
