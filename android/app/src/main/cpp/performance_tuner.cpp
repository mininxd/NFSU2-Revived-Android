#include "performance_tuner.h"
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include <android/log.h>
#include <cstdio>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

namespace nfs_perf {

static cpu_set_t s_perf_mask;
static cpu_set_t s_little_mask;
static cpu_set_t s_all_mask;
static bool s_topology_initialized = false;
static int s_total_cores = 0;
static int s_perf_cores_count = 0;

void initTopology() {
    if (s_topology_initialized) return;

    CPU_ZERO(&s_perf_mask);
    CPU_ZERO(&s_little_mask);
    CPU_ZERO(&s_all_mask);

    long num_cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (num_cpus <= 0) num_cpus = 8;
    if (num_cpus > CPU_SETSIZE) num_cpus = CPU_SETSIZE;
    s_total_cores = static_cast<int>(num_cpus);

    std::vector<std::pair<int, unsigned long>> core_freqs;
    unsigned long max_seen_freq = 0;

    for (int i = 0; i < s_total_cores; ++i) {
        CPU_SET(i, &s_all_mask);
        char path[128];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE* fp = std::fopen(path, "r");
        unsigned long freq = 0;
        if (fp) {
            if (std::fscanf(fp, "%lu", &freq) == 1) {
                if (freq > max_seen_freq) max_seen_freq = freq;
            }
            std::fclose(fp);
        }
        core_freqs.push_back({i, freq});
    }

    if (max_seen_freq > 0) {
        // Group cores with frequency >= 75% of maximum frequency into the performance cluster
        unsigned long threshold = (max_seen_freq * 3) / 4;
        for (const auto& [core_id, freq] : core_freqs) {
            if (freq >= threshold) {
                CPU_SET(core_id, &s_perf_mask);
                ++s_perf_cores_count;
            } else {
                CPU_SET(core_id, &s_little_mask);
            }
        }
    }

    // Fallback if sysfs cpuinfo_max_freq is inaccessible
    if (s_perf_cores_count == 0) {
        if (s_total_cores >= 8) {
            // Standard octa-core big.LITTLE: upper 4 cores are performance (e.g. 4+4 or 4+3+1)
            for (int i = 4; i < s_total_cores; ++i) {
                CPU_SET(i, &s_perf_mask);
                ++s_perf_cores_count;
            }
            for (int i = 0; i < 4; ++i) {
                CPU_SET(i, &s_little_mask);
            }
        } else if (s_total_cores >= 4) {
            for (int i = s_total_cores / 2; i < s_total_cores; ++i) {
                CPU_SET(i, &s_perf_mask);
                ++s_perf_cores_count;
            }
            for (int i = 0; i < s_total_cores / 2; ++i) {
                CPU_SET(i, &s_little_mask);
            }
        } else {
            s_perf_mask = s_all_mask;
            s_perf_cores_count = s_total_cores;
        }
    }

    s_topology_initialized = true;
    __android_log_print(ANDROID_LOG_INFO, "NFSU2", "CPU Topology initialized: %d total cores, %d performance cores",
                        s_total_cores, s_perf_cores_count);
}

void tuneThread(ThreadRole role, const char* name) {
    if (!s_topology_initialized) initTopology();

    if (name && name[0] != '\0') {
        pthread_setname_np(pthread_self(), name);
    }

    switch (role) {
        case ThreadRole::RENDER_MAIN: {
            // Pin render thread exclusively to big/prime performance cores
            int rc = sched_setaffinity(0, sizeof(cpu_set_t), &s_perf_mask);
            if (rc != 0) {
                __android_log_print(ANDROID_LOG_WARN, "NFSU2", "sched_setaffinity to perf cores returned %d", rc);
            }
            setpriority(PRIO_PROCESS, 0, -10);
            break;
        }
        case ThreadRole::GUEST_AUDIO: {
            setpriority(PRIO_PROCESS, 0, -5);
            break;
        }
        case ThreadRole::BACKGROUND_WORKER: {
            (void)s_little_mask;
            sched_setaffinity(0, sizeof(cpu_set_t), &s_all_mask);
            setpriority(PRIO_PROCESS, 0, 0);
            break;
        }
    }
}

std::string getTopologySummary() {
    if (!s_topology_initialized) initTopology();
    std::ostringstream ss;
    ss << "Total: " << s_total_cores << " cores, Perf cluster: " << s_perf_cores_count << " cores";
    return ss.str();
}

} // namespace nfs_perf
