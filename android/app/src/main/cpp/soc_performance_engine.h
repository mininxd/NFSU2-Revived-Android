#pragma once
#include <android/native_window.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sched.h>
#include <unistd.h>

namespace nfs_soc {

inline void boostRenderThread() {
    typedef void (*FnBoost)();
    static FnBoost s_rust_boost = nullptr;
    static bool s_checked = false;

    if (!s_checked) {
        s_checked = true;
        void* handle = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libperf_cores.so", RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            s_rust_boost = reinterpret_cast<FnBoost>(dlsym(handle, "nfs_soc_boost_current_render_thread"));
        }
        if (!s_rust_boost) {
            s_rust_boost = reinterpret_cast<FnBoost>(dlsym(RTLD_DEFAULT, "nfs_soc_boost_current_render_thread"));
        }
    }

    if (s_rust_boost) {
        s_rust_boost();
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-SoC", "Boosted render thread via SoC tuner (Big/Prime core lock)");
    } else {
        // Fallback: set nice -20 and timer slack
        setpriority(PRIO_PROCESS, 0, -20);
#ifdef PR_SET_TIMERSLACK
        prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
    }
}

inline void enforceWindowFrameRate(ANativeWindow* window, float targetFps = 60.0f) {
    if (!window) return;

    typedef int32_t (*PFN_ANativeWindow_setFrameRateWithChangeFrameRateStrategy)(ANativeWindow*, float, int8_t, int8_t);
    typedef int32_t (*PFN_ANativeWindow_setFrameRate)(ANativeWindow*, float, int8_t);

    static auto pfnStrategy = reinterpret_cast<PFN_ANativeWindow_setFrameRateWithChangeFrameRateStrategy>(
        dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRateWithChangeFrameRateStrategy"));
    static auto pfnSetRate = reinterpret_cast<PFN_ANativeWindow_setFrameRate>(
        dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));

    if (pfnStrategy) {
        // ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT = 0
        // ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS = 1
        pfnStrategy(window, targetFps, 0, 1);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-SoC", "Enforced window frame rate %.1f FPS (Strategy: ALWAYS)", targetFps);
    } else if (pfnSetRate) {
        pfnSetRate(window, targetFps, 0);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-SoC", "Enforced window frame rate %.1f FPS", targetFps);
    }
}

} // namespace nfs_soc
