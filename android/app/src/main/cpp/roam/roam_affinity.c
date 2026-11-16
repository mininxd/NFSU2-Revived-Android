/* roam_affinity.c -- Performance core CPU affinity management for Android */
#include "roam_affinity.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sched.h>
#include <android/log.h>

int nfs_bind_to_perf_cores(void) {
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
    long threshold = (long)(highest_freq * 0.80);

    if (highest_freq > 0) {
        for (int i = 0; i < total_cpus; i++) {
            if (freqs[i] >= threshold) {
                CPU_SET(i, &cpuset);
                count++;
            }
        }
    }

    if (count == 0) {
        int start = total_cpus > 1 ? total_cpus / 2 : 0;
        for (int i = start; i < total_cpus; i++) {
            CPU_SET(i, &cpuset);
            count++;
        }
    }

    sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    // Apply performance core affinity to all worker threads of the process
    DIR *d = opendir("/proc/self/task");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            int tid = atoi(de->d_name);
            if (tid > 0) {
                sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
            }
        }
        closedir(d);
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-PerfCores", "Bound process and threads to %d performance cores (ret=%d)", count, ret);
    return ret;
}
