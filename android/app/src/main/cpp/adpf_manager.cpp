#include "adpf_manager.h"
#include <dlfcn.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <time.h>
#include <vector>
#include <atomic>
#include <android/log.h>

#define TAG "NFSU2-ADPF"

// Opaque types from <android/performance_hint.h>
typedef void APerformanceHintManager;
typedef void APerformanceHintSession;
typedef void AWorkDuration;

typedef APerformanceHintManager* (*pfn_APerformanceHint_getManager)();
typedef APerformanceHintSession* (*pfn_APerformanceHint_createSession)(
    APerformanceHintManager*, const int32_t*, size_t, int64_t);
typedef void (*pfn_APerformanceHint_updateTargetWorkDuration)(APerformanceHintSession*, int64_t);
typedef void (*pfn_APerformanceHint_reportActualWorkDuration)(APerformanceHintSession*, int64_t);
typedef void (*pfn_APerformanceHint_closeSession)(APerformanceHintSession*);

// API 34+ unified CPU + GPU hint reporting
typedef AWorkDuration* (*pfn_AWorkDuration_create)();
typedef void (*pfn_AWorkDuration_release)(AWorkDuration*);
typedef void (*pfn_AWorkDuration_setWorkPeriodStartTimestampNanos)(AWorkDuration*, int64_t);
typedef void (*pfn_AWorkDuration_setActualTotalDurationNanos)(AWorkDuration*, int64_t);
typedef void (*pfn_AWorkDuration_setActualCpuDurationNanos)(AWorkDuration*, int64_t);
typedef void (*pfn_AWorkDuration_setActualGpuDurationNanos)(AWorkDuration*, int64_t);
typedef int (*pfn_APerformanceHint_reportActualWorkDuration2)(APerformanceHintSession*, AWorkDuration*);

static APerformanceHintManager* s_hint_manager = nullptr;
static APerformanceHintSession* s_hint_session = nullptr;
static std::atomic<int64_t> s_target_frame_ns{16666666}; // Default 60 FPS (16.6ms)
static std::atomic<int64_t> s_cpu_start_ns{0};
static bool s_symbols_loaded = false;

static pfn_APerformanceHint_getManager s_pfn_get_manager = nullptr;
static pfn_APerformanceHint_createSession s_pfn_create_session = nullptr;
static pfn_APerformanceHint_updateTargetWorkDuration s_pfn_update_target = nullptr;
static pfn_APerformanceHint_reportActualWorkDuration s_pfn_report_actual = nullptr;
static pfn_APerformanceHint_closeSession s_pfn_close_session = nullptr;

static pfn_AWorkDuration_create s_pfn_work_create = nullptr;
static pfn_AWorkDuration_release s_pfn_work_release = nullptr;
static pfn_AWorkDuration_setWorkPeriodStartTimestampNanos s_pfn_work_set_start = nullptr;
static pfn_AWorkDuration_setActualTotalDurationNanos s_pfn_work_set_total = nullptr;
static pfn_AWorkDuration_setActualCpuDurationNanos s_pfn_work_set_cpu = nullptr;
static pfn_AWorkDuration_setActualGpuDurationNanos s_pfn_work_set_gpu = nullptr;
static pfn_APerformanceHint_reportActualWorkDuration2 s_pfn_report_actual2 = nullptr;

static inline int64_t getNowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static void load_adpf_symbols() {
    if (s_symbols_loaded) return;
    s_symbols_loaded = true;

    void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return;

    s_pfn_get_manager = (pfn_APerformanceHint_getManager)dlsym(lib, "APerformanceHint_getManager");
    s_pfn_create_session = (pfn_APerformanceHint_createSession)dlsym(lib, "APerformanceHint_createSession");
    s_pfn_update_target = (pfn_APerformanceHint_updateTargetWorkDuration)dlsym(lib, "APerformanceHint_updateTargetWorkDuration");
    s_pfn_report_actual = (pfn_APerformanceHint_reportActualWorkDuration)dlsym(lib, "APerformanceHint_reportActualWorkDuration");
    s_pfn_close_session = (pfn_APerformanceHint_closeSession)dlsym(lib, "APerformanceHint_closeSession");

    // Optional API 34+ unified CPU + GPU functions
    s_pfn_work_create = (pfn_AWorkDuration_create)dlsym(lib, "AWorkDuration_create");
    s_pfn_work_release = (pfn_AWorkDuration_release)dlsym(lib, "AWorkDuration_release");
    s_pfn_work_set_start = (pfn_AWorkDuration_setWorkPeriodStartTimestampNanos)dlsym(lib, "AWorkDuration_setWorkPeriodStartTimestampNanos");
    s_pfn_work_set_total = (pfn_AWorkDuration_setActualTotalDurationNanos)dlsym(lib, "AWorkDuration_setActualTotalDurationNanos");
    s_pfn_work_set_cpu = (pfn_AWorkDuration_setActualCpuDurationNanos)dlsym(lib, "AWorkDuration_setActualCpuDurationNanos");
    s_pfn_work_set_gpu = (pfn_AWorkDuration_setActualGpuDurationNanos)dlsym(lib, "AWorkDuration_setActualGpuDurationNanos");
    s_pfn_report_actual2 = (pfn_APerformanceHint_reportActualWorkDuration2)dlsym(lib, "APerformanceHint_reportActualWorkDuration2");

    if (s_pfn_get_manager) {
        s_hint_manager = s_pfn_get_manager();
        if (s_hint_manager) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "APerformanceHintManager initialized successfully (API 34+ boost=%s)",
                                s_pfn_report_actual2 ? "available" : "standard");
        }
    }
}

static std::vector<int32_t> collect_critical_tids() {
    std::vector<int32_t> tids;
    DIR* d = opendir("/proc/self/task");
    if (d) {
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            int tid = atoi(de->d_name);
            if (tid > 0) {
                tids.push_back(tid);
            }
        }
        closedir(d);
    }
    if (tids.empty()) {
        tids.push_back((int32_t)syscall(SYS_gettid));
    }
    return tids;
}

// Pass all critical thread TIDs (main/game thread, render thread, heavy worker threads)
void initAdpfSession(const std::vector<int32_t>& criticalTids) {
    load_adpf_symbols();
    if (!s_hint_manager || !s_pfn_create_session) return;

    if (s_hint_session && s_pfn_close_session) {
        s_pfn_close_session(s_hint_session);
        s_hint_session = nullptr;
    }

    int64_t target_ns = s_target_frame_ns.load();
    s_hint_session = s_pfn_create_session(
        s_hint_manager,
        criticalTids.data(),
        criticalTids.size(),
        target_ns
    );
    if (s_hint_session) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "Created ADPF hint session with %zu threads, target=%lld ns",
                            criticalTids.size(), (long long)target_ns);
    }
}

// Call every frame: measure CPU work + GPU command execution time and command governor boost
void reportFrameLoad(int64_t cpuStartNs, int64_t cpuEndNs, int64_t gpuDurationNs) {
    if (!s_hint_session) return;

    int64_t cpuDurationNs = cpuEndNs - cpuStartNs;
    if (cpuDurationNs <= 0) cpuDurationNs = 1;

    int64_t totalDurationNs = cpuDurationNs + gpuDurationNs;
    int64_t target_ns = s_target_frame_ns.load();

    // Force governor to boost CPU and GPU clocks:
    // If work duration is low, the governor may try to downclock to save power.
    // Reporting work load at or above target budget signals urgency and forces high frequency scaling!
    if (totalDurationNs < target_ns) {
        totalDurationNs = target_ns + 500000LL; // Signal high performance demand
    }

    // API 34+ unified CPU + GPU hint (tells governor to boost both CPU & GPU clocks)
    if (s_pfn_report_actual2 && s_pfn_work_create) {
        AWorkDuration* duration = s_pfn_work_create();
        if (duration) {
            if (s_pfn_work_set_start) s_pfn_work_set_start(duration, cpuStartNs);
            if (s_pfn_work_set_total) s_pfn_work_set_total(duration, totalDurationNs);
            if (s_pfn_work_set_cpu) s_pfn_work_set_cpu(duration, cpuDurationNs);
            if (s_pfn_work_set_gpu) s_pfn_work_set_gpu(duration, gpuDurationNs > 0 ? gpuDurationNs : (totalDurationNs - cpuDurationNs));

            s_pfn_report_actual2(s_hint_session, duration);
            if (s_pfn_work_release) s_pfn_work_release(duration);
            return;
        }
    }

    if (s_pfn_report_actual) {
        s_pfn_report_actual(s_hint_session, totalDurationNs);
    }
}

extern "C" {

typedef void (*pfn_adpf_init)(int);
typedef void (*pfn_adpf_void)(void);

static void* get_perf_cores_handle() {
    static void* handle = nullptr;
    if (!handle) {
        handle = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libperf_cores.so", RTLD_NOW | RTLD_GLOBAL);
    }
    return handle;
}

void nfs_adpf_init(int target_fps) {
    int fps = (target_fps > 0) ? target_fps : 60;
    int64_t target_ns = 1000000000LL / fps;
    s_target_frame_ns.store(target_ns);

    // 1. Initialize Rust ADPF if available
    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_init>(dlsym(h, "nfs_adpf_init")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_init>(dlsym(RTLD_DEFAULT, "nfs_adpf_init"));
    if (fn && fn != &nfs_adpf_init) {
        fn(fps);
    }

    // 2. Initialize native C++ ADPF session covering all critical thread TIDs
    std::vector<int32_t> tids = collect_critical_tids();
    initAdpfSession(tids);
}

void nfs_adpf_begin_frame(void) {
    s_cpu_start_ns.store(getNowNs());

    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_void>(dlsym(h, "nfs_adpf_begin_frame")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_void>(dlsym(RTLD_DEFAULT, "nfs_adpf_begin_frame"));
    if (fn && fn != &nfs_adpf_begin_frame) {
        fn();
    }
}

void nfs_adpf_end_frame(void) {
    int64_t start_ns = s_cpu_start_ns.load();
    int64_t end_ns = getNowNs();
    int64_t gpu_dur_ns = (s_target_frame_ns.load() / 2); // Approximate 50% frame GPU slice
    reportFrameLoad(start_ns > 0 ? start_ns : end_ns - 1000000, end_ns, gpu_dur_ns);

    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_void>(dlsym(h, "nfs_adpf_end_frame")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_void>(dlsym(RTLD_DEFAULT, "nfs_adpf_end_frame"));
    if (fn && fn != &nfs_adpf_end_frame) {
        fn();
    }
}

void nfs_adpf_destroy(void) {
    if (s_hint_session && s_pfn_close_session) {
        s_pfn_close_session(s_hint_session);
        s_hint_session = nullptr;
    }

    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_void>(dlsym(h, "nfs_adpf_destroy")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_void>(dlsym(RTLD_DEFAULT, "nfs_adpf_destroy"));
    if (fn && fn != &nfs_adpf_destroy) {
        fn();
    }
}

}
