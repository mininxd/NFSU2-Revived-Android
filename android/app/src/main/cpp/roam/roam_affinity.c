#define _GNU_SOURCE
/* roam_affinity.c -- Performance core CPU affinity management for Android */
#include "roam_affinity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sched.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <android/log.h>

static int contains_case_insensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return 1;
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) return 1;
    }
    return 0;
}

int nfs_bind_to_perf_cores(void) {
    typedef int (*FnBindPerf)(void);
    static FnBindPerf s_rust_bind = NULL;
    static int s_checked = 0;
    if (!s_checked) {
        s_checked = 1;
        void* h = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libperf_cores.so", RTLD_NOW);
        if (h) s_rust_bind = (FnBindPerf)dlsym(h, "nfs_perf_cores_bind");
        if (!s_rust_bind) s_rust_bind = (FnBindPerf)dlsym(RTLD_DEFAULT, "nfs_perf_cores_bind");
    }
    if (s_rust_bind) {
        int r = s_rust_bind();
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-PerfCores", "Bound process to tri-cluster performance cores via Rust engine (ret=%d)", r);
        return r;
    }

    long highest_freq = 0;
    long freqs[64] = {0};
    int total_cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (total_cpus <= 0 || total_cpus > 64) total_cpus = 8;

    for (int i = 0; i < total_cpus; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *f = fopen(path, "r");
        if (!f) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
            f = fopen(path, "r");
        }
        if (!f) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", i);
            f = fopen(path, "r");
            if (f) {
                long cap = 0;
                if (fscanf(f, "%ld", &cap) == 1 && cap > 0) {
                    freqs[i] = cap * 1000;
                    if (freqs[i] > highest_freq) highest_freq = freqs[i];
                }
                fclose(f);
                continue;
            }
        }
        if (f) {
            long freq = 0;
            if (fscanf(f, "%ld", &freq) == 1 && freq > 0) {
                freqs[i] = freq;
                if (freq > highest_freq) highest_freq = freq;
            }
            fclose(f);
        }
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int count = 0;

    // Filter performance cores (Prime + Mid clusters)
    // On big.LITTLE / tri-cluster architectures, cores with >= 70% of max frequency or capacity
    // represent the mid/big and prime cores, while little efficiency cores are excluded.
    long threshold = (long)(highest_freq * 0.70);
    int is_all_big_soc = 0;
    FILE *f_cpu = fopen("/proc/cpuinfo", "r");
    if (f_cpu) {
        char line[256];
        while (fgets(line, sizeof(line), f_cpu)) {
            if (contains_case_insensitive(line, "sm8750") || contains_case_insensitive(line, "sun") || contains_case_insensitive(line, "oryon") ||
                contains_case_insensitive(line, "mt6989") || contains_case_insensitive(line, "mt6991") ||
                contains_case_insensitive(line, "dimensity 9300") || contains_case_insensitive(line, "dimensity 9400")) {
                is_all_big_soc = 1;
                break;
            }
        }
        fclose(f_cpu);
    }

    if (highest_freq > 0 && !is_all_big_soc) {
        for (int i = 0; i < total_cpus; i++) {
            if (freqs[i] >= threshold) {
                CPU_SET(i, &cpuset);
                count++;
            }
        }
    }

    // Safety fallback: if all-big SoC, or thresholding produced 0 cores or all cores, use all cores
    if (count == 0 || is_all_big_soc) {
        CPU_ZERO(&cpuset);
        count = 0;
        for (int i = 0; i < total_cpus; i++) {
            CPU_SET(i, &cpuset);
            count++;
        }
    }

    // Elevate priority of the process & calling thread to highest real-time interactive priority (-20)
    // so EAS / WALT governors prioritize scheduling and rapidly ramp up CPU clocks.
    setpriority(PRIO_PROCESS, getpid(), -20);
    setpriority(PRIO_PROCESS, 0, -20);

    sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    // Apply performance core affinity and elevated priority to all worker threads of the process
    DIR *d = opendir("/proc/self/task");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            int tid = atoi(de->d_name);
            if (tid > 0) {
                sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
                setpriority(PRIO_PROCESS, tid, -20);
            }
        }
        closedir(d);
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-PerfCores", "Bound process and threads to %d performance cores (highest_freq=%ld, ret=%d)", count, highest_freq, ret);
    return ret;
}
