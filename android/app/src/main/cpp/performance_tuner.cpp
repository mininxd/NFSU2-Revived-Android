#include "performance_tuner.h"
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include <android/log.h>
#include <dlfcn.h>
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
        if (freq == 0) {
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", i);
            fp = std::fopen(path, "r");
            if (fp) {
                unsigned long cap = 0;
                if (std::fscanf(fp, "%lu", &cap) == 1) {
                    freq = cap * 1000;
                    if (freq > max_seen_freq) max_seen_freq = freq;
                }
                std::fclose(fp);
            }
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

    // Secondary detection: Parse /proc/cpuinfo (always permitted by SELinux on Android 14)
    if (s_perf_cores_count == 0) {
        FILE* cpuinfo = std::fopen("/proc/cpuinfo", "r");
        if (cpuinfo) {
            char line[256];
            int cur_cpu = -1;
            while (std::fgets(line, sizeof(line), cpuinfo)) {
                if (std::strncmp(line, "processor", 9) == 0) {
                    char* colon = std::strchr(line, ':');
                    if (colon) cur_cpu = std::atoi(colon + 1);
                } else if (std::strncmp(line, "CPU part", 8) == 0 && cur_cpu >= 0 && cur_cpu < s_total_cores) {
                    char* colon = std::strchr(line, ':');
                    if (colon) {
                        unsigned long part = std::strtoul(colon + 1, nullptr, 16);
                        bool is_little = (part == 0xd03 || part == 0xd05 || part == 0xd46 || part == 0xd80);
                        if (part >= 0xd08 && !is_little) {
                            CPU_SET(cur_cpu, &s_perf_mask);
                            ++s_perf_cores_count;
                        } else {
                            CPU_SET(cur_cpu, &s_little_mask);
                        }
                    }
                }
            }
            std::fclose(cpuinfo);
        }
    }

    // Universal multi-core fallback: upper half of CPU cores
    if (s_perf_cores_count == 0) {
        if (s_total_cores >= 4) {
            int start = s_total_cores / 2;
            for (int i = start; i < s_total_cores; ++i) {
                CPU_SET(i, &s_perf_mask);
                ++s_perf_cores_count;
            }
            for (int i = 0; i < start; ++i) CPU_SET(i, &s_little_mask);
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
    typedef void (*FnTuneRole)(int, const char*);
    static FnTuneRole s_rust_tune = nullptr;
    static bool s_checked = false;
    if (!s_checked) {
        s_checked = true;
        void* handle = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libperf_cores.so", RTLD_NOW);
        if (handle) {
            s_rust_tune = reinterpret_cast<FnTuneRole>(dlsym(handle, "nfs_perf_tune_thread_role"));
        }
        if (!s_rust_tune) {
            s_rust_tune = reinterpret_cast<FnTuneRole>(dlsym(RTLD_DEFAULT, "nfs_perf_tune_thread_role"));
        }
    }

    if (s_rust_tune) {
        s_rust_tune(static_cast<int>(role), name);
        return;
    }

    if (!s_topology_initialized) initTopology();

    if (name && name[0] != '\0') {
        pthread_setname_np(pthread_self(), name);
    }

    switch (role) {
        case ThreadRole::RENDER_MAIN: {
            if (s_perf_cores_count > 0) {
                sched_setaffinity(0, sizeof(cpu_set_t), &s_perf_mask);
            }
            setpriority(PRIO_PROCESS, 0, -16);
            break;
        }
        case ThreadRole::GUEST_AUDIO: {
            if (s_perf_cores_count > 0) {
                sched_setaffinity(0, sizeof(cpu_set_t), &s_perf_mask);
            }
            setpriority(PRIO_PROCESS, 0, -12);
            break;
        }
        case ThreadRole::BACKGROUND_WORKER: {
            sched_setaffinity(0, sizeof(cpu_set_t), &s_all_mask);
            setpriority(PRIO_PROCESS, 0, 0);
            break;
        }
    }
}

std::string getTopologySummary() {
    typedef int (*FnSummaryBuf)(char*, size_t);
    static FnSummaryBuf s_rust_summary = nullptr;
    static bool s_checked = false;
    if (!s_checked) {
        s_checked = true;
        void* handle = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libperf_cores.so", RTLD_NOW);
        if (handle) {
            s_rust_summary = reinterpret_cast<FnSummaryBuf>(dlsym(handle, "nfs_perf_get_topology_summary_buf"));
        }
        if (!s_rust_summary) {
            s_rust_summary = reinterpret_cast<FnSummaryBuf>(dlsym(RTLD_DEFAULT, "nfs_perf_get_topology_summary_buf"));
        }
    }

    if (s_rust_summary) {
        char buf[256];
        if (s_rust_summary(buf, sizeof(buf)) > 0) {
            return std::string(buf);
        }
    }

    if (!s_topology_initialized) initTopology();
    std::ostringstream ss;
    ss << "Total: " << s_total_cores << " cores, Perf cluster: " << s_perf_cores_count << " cores";
    return ss.str();
}

} // namespace nfs_perf
