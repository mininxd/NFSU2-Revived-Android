#include "adpf_manager.h"
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <time.h>
#include <atomic>

#define TAG "NFSU2-ADPF"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;

typedef APerformanceHintManager* (*pfn_APerformanceHint_getManager)();
typedef APerformanceHintSession* (*pfn_APerformanceHint_createSession)(
    APerformanceHintManager*, const int32_t*, size_t, int64_t);
typedef void (*pfn_APerformanceHint_updateTargetWorkDuration)(APerformanceHintSession*, int64_t);
typedef void (*pfn_APerformanceHint_reportActualWorkDuration)(APerformanceHintSession*, int64_t);
typedef void (*pfn_APerformanceHint_closeSession)(APerformanceHintSession*);

static pfn_APerformanceHint_getManager s_getManager = nullptr;
static pfn_APerformanceHint_createSession s_createSession = nullptr;
static pfn_APerformanceHint_updateTargetWorkDuration s_updateTarget = nullptr;
static pfn_APerformanceHint_reportActualWorkDuration s_reportActual = nullptr;
static pfn_APerformanceHint_closeSession s_closeSession = nullptr;

static APerformanceHintManager* s_manager = nullptr;
static APerformanceHintSession* s_session = nullptr;
static int64_t s_target_duration_ns = 16666666LL; // Default 60 FPS (16.6ms)
static int64_t s_frame_start_ns = 0;
static bool s_initialized = false;
static bool s_supported = false;

static inline int64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static void load_adpf_symbols() {
    if (s_initialized) return;
    s_initialized = true;

    void* libandroid = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!libandroid) {
        LOGW("Failed to dlopen libandroid.so for ADPF");
        return;
    }

    s_getManager = reinterpret_cast<pfn_APerformanceHint_getManager>(
        dlsym(libandroid, "APerformanceHint_getManager"));
    s_createSession = reinterpret_cast<pfn_APerformanceHint_createSession>(
        dlsym(libandroid, "APerformanceHint_createSession"));
    s_updateTarget = reinterpret_cast<pfn_APerformanceHint_updateTargetWorkDuration>(
        dlsym(libandroid, "APerformanceHint_updateTargetWorkDuration"));
    s_reportActual = reinterpret_cast<pfn_APerformanceHint_reportActualWorkDuration>(
        dlsym(libandroid, "APerformanceHint_reportActualWorkDuration"));
    s_closeSession = reinterpret_cast<pfn_APerformanceHint_closeSession>(
        dlsym(libandroid, "APerformanceHint_closeSession"));

    if (s_getManager && s_createSession && s_reportActual && s_closeSession) {
        s_manager = s_getManager();
        if (s_manager) {
            s_supported = true;
            LOGI("ADPF (Android Dynamic Performance Framework) successfully initialized");
        } else {
            LOGW("APerformanceHint_getManager returned null (not supported by kernel/device)");
        }
    } else {
        LOGI("ADPF APIs not available on this Android API level (< 31)");
    }
}

void nfs_adpf_init(int target_fps) {
    load_adpf_symbols();
    if (!s_supported || !s_manager) return;

    if (target_fps <= 0) target_fps = 60;
    s_target_duration_ns = 1000000000LL / target_fps;

    if (s_session && s_closeSession) {
        s_closeSession(s_session);
        s_session = nullptr;
    }

    int32_t tids[1] = { static_cast<int32_t>(gettid()) };
    s_session = s_createSession(s_manager, tids, 1, s_target_duration_ns);
    if (s_session) {
        LOGI("ADPF session active for thread tid=%d, target=%lld ns (%d FPS)",
             tids[0], static_cast<long long>(s_target_duration_ns), target_fps);
    } else {
        LOGW("Failed to create ADPF hint session");
    }
}

void nfs_adpf_begin_frame(void) {
    s_frame_start_ns = get_time_ns();
}

void nfs_adpf_end_frame(void) {
    if (!s_session || !s_reportActual || s_frame_start_ns == 0) return;
    int64_t actual_duration_ns = get_time_ns() - s_frame_start_ns;
    if (actual_duration_ns > 0 && actual_duration_ns < 1000000000LL) {
        s_reportActual(s_session, actual_duration_ns);
    }
}

void nfs_adpf_destroy(void) {
    if (s_session && s_closeSession) {
        s_closeSession(s_session);
        s_session = nullptr;
    }
}
