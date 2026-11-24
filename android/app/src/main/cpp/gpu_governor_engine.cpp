#include "gpu_governor_engine.h"
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <android/log.h>

#define TAG "NFSU2-GpuEngine"

namespace nfs_gpu {

typedef void (*pfn_gpu_init)();
typedef void (*pfn_gpu_lock)();
typedef void (*pfn_gpu_state)(int);
typedef void (*pfn_gpu_tick)(int);
typedef int (*pfn_gpu_get_freq)();

static pfn_gpu_init s_fn_init = nullptr;
static pfn_gpu_lock s_fn_lock = nullptr;
static pfn_gpu_state s_fn_state = nullptr;
static pfn_gpu_tick s_fn_tick = nullptr;
static pfn_gpu_get_freq s_fn_get_freq = nullptr;
static bool s_symbols_resolved = false;

static void* get_perf_cores_handle() {
    static void* handle = nullptr;
    if (!handle) {
        handle = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libperf_cores.so", RTLD_NOW | RTLD_GLOBAL);
    }
    return handle;
}

static void resolve_symbols() {
    if (s_symbols_resolved) return;
    s_symbols_resolved = true;

    void* h = get_perf_cores_handle();
    if (h) {
        s_fn_init = reinterpret_cast<pfn_gpu_init>(dlsym(h, "nfs_gpu_governor_init"));
        s_fn_lock = reinterpret_cast<pfn_gpu_lock>(dlsym(h, "nfs_gpu_governor_lock_peak"));
        s_fn_state = reinterpret_cast<pfn_gpu_state>(dlsym(h, "nfs_gpu_governor_set_gameplay_state"));
        s_fn_tick = reinterpret_cast<pfn_gpu_tick>(dlsym(h, "nfs_gpu_governor_tick"));
        s_fn_get_freq = reinterpret_cast<pfn_gpu_get_freq>(dlsym(h, "nfs_gpu_governor_get_locked_freq_mhz"));
    }

    if (!s_fn_init) s_fn_init = reinterpret_cast<pfn_gpu_init>(dlsym(RTLD_DEFAULT, "nfs_gpu_governor_init"));
    if (!s_fn_lock) s_fn_lock = reinterpret_cast<pfn_gpu_lock>(dlsym(RTLD_DEFAULT, "nfs_gpu_governor_lock_peak"));
    if (!s_fn_state) s_fn_state = reinterpret_cast<pfn_gpu_state>(dlsym(RTLD_DEFAULT, "nfs_gpu_governor_set_gameplay_state"));
    if (!s_fn_tick) s_fn_tick = reinterpret_cast<pfn_gpu_tick>(dlsym(RTLD_DEFAULT, "nfs_gpu_governor_tick"));
    if (!s_fn_get_freq) s_fn_get_freq = reinterpret_cast<pfn_gpu_get_freq>(dlsym(RTLD_DEFAULT, "nfs_gpu_governor_get_locked_freq_mhz"));
}

static void direct_sysfs_write(const char* path, const char* val) {
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        ssize_t n = write(fd, val, strlen(val));
        (void)n;
        close(fd);
    }
}

static void apply_native_sysfs_clamp(bool isGameplay) {
    const char* kgsl_paths[] = {
        "/sys/class/kgsl/kgsl-3d0",
        "/sys/devices/platform/soc/soc:qcom,kgsl-3d0/kgsl/kgsl-3d0",
        "/sys/devices/platform/soc/1c00000.qcom,kgsl-3d0/kgsl/kgsl-3d0",
        "/sys/devices/platform/soc/3d00000.qcom,kgsl-3d0/kgsl/kgsl-3d0",
    };

    char buf[256];
    for (const char* base : kgsl_paths) {
        snprintf(buf, sizeof(buf), "%s/min_pwrlevel", base);
        direct_sysfs_write(buf, "0");

        snprintf(buf, sizeof(buf), "%s/default_pwrlevel", base);
        direct_sysfs_write(buf, "0");

        // Extend idle timer to 1000ms so 20-30ms CPU emulation delay doesn't downclock GPU
        snprintf(buf, sizeof(buf), "%s/idle_timer", base);
        direct_sysfs_write(buf, isGameplay ? "1000" : "500");

        snprintf(buf, sizeof(buf), "%s/force_rail_on", base);
        direct_sysfs_write(buf, "1");

        snprintf(buf, sizeof(buf), "%s/force_clk_on", base);
        direct_sysfs_write(buf, "1");

        snprintf(buf, sizeof(buf), "%s/force_bus_on", base);
        direct_sysfs_write(buf, "1");

        snprintf(buf, sizeof(buf), "%s/devfreq/governor", base);
        direct_sysfs_write(buf, "performance");
    }

    // MediaTek GED
    direct_sysfs_write("/sys/module/ged/parameters/boost_gpu_enable", "1");
    direct_sysfs_write("/sys/module/ged/parameters/ged_boost_enable", "1");
    direct_sysfs_write("/sys/module/ged/parameters/gx_boost_on", "1");
    direct_sysfs_write("/sys/module/ged/parameters/gx_force_cpu_boost", "1");
    direct_sysfs_write("/sys/kernel/debug/ged/hal/gpu_boost_level", "100");
}

void init() {
    resolve_symbols();
    if (s_fn_init) {
        s_fn_init();
    }
    apply_native_sysfs_clamp(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Initialized native GPU governor engine");
}

void lockPeakClock(bool isGameplay) {
    resolve_symbols();
    if (s_fn_lock) {
        s_fn_lock();
    }
    apply_native_sysfs_clamp(isGameplay);
}

void setGameplayState(bool inGameplay) {
    resolve_symbols();
    if (s_fn_state) {
        s_fn_state(inGameplay ? 1 : 0);
    }
    apply_native_sysfs_clamp(inGameplay);
}

void onFrameTick(bool inGameplay) {
    resolve_symbols();
    if (s_fn_tick) {
        s_fn_tick(inGameplay ? 1 : 0);
    }
}

int getLockedFreqMhz() {
    resolve_symbols();
    if (s_fn_get_freq) {
        return s_fn_get_freq();
    }
    return 0;
}

} // namespace nfs_gpu
