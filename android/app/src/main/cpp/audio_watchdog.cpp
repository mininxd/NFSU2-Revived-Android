#include "audio_watchdog.h"
#include <dlfcn.h>
#include <thread>
#include <pthread.h>
#include <android/log.h>

namespace nfs_audio_watchdog {

static void* get_graphics_cache_handle() {
    static void* handle = nullptr;
    if (!handle) {
        handle = dlopen("libgraphics_cache.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libgraphics_cache.so", RTLD_NOW | RTLD_GLOBAL);
    }
    return handle;
}

void notifyCallbackRendered() {
    typedef void (*FnNotify)();
    static auto fn = reinterpret_cast<FnNotify>(dlsym(get_graphics_cache_handle(), "nfs_audio_watchdog_notify"));
    if (fn) fn();
}

void serviceSyntheticClock(PcmMixer& mixer) {
    typedef uint32_t (*FnAdvance)(uint32_t);
    static auto fn = reinterpret_cast<FnAdvance>(dlsym(get_graphics_cache_handle(), "nfs_audio_watchdog_synthetic_advance"));
    if (fn) {
        uint32_t rate = mixer.rate ? mixer.rate : 48000;
        uint32_t missed = fn(rate);
        if (missed > 0) {
            mixer.advance(missed);
        }
    }
}

bool isCallbackStalled() {
    typedef int (*FnStalled)();
    static auto fn = reinterpret_cast<FnStalled>(dlsym(get_graphics_cache_handle(), "nfs_audio_watchdog_stalled"));
    return fn ? (fn() != 0) : false;
}

bool canAttemptReconnect() {
    typedef int (*FnCan)();
    static auto fn = reinterpret_cast<FnCan>(dlsym(get_graphics_cache_handle(), "nfs_audio_watchdog_can_reconnect"));
    return fn ? (fn() != 0) : true;
}

void recordReconnectAttempt() {
    typedef void (*FnRecord)();
    static auto fn = reinterpret_cast<FnRecord>(dlsym(get_graphics_cache_handle(), "nfs_audio_watchdog_record_reconnect"));
    if (fn) fn();
}

static std::thread s_watchdogThread;
static std::atomic<bool> s_watchdogRunning{false};

void startWatchdogThread(PcmMixer& mixer) {
    if (s_watchdogRunning.exchange(true, std::memory_order_acq_rel)) return;
    s_watchdogThread = std::thread([&mixer]() {
        pthread_setname_np(pthread_self(), "nfs_audio_wdog");
        while (s_watchdogRunning.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            serviceSyntheticClock(mixer);
        }
    });
}

void stopWatchdogThread() {
    if (!s_watchdogRunning.exchange(false, std::memory_order_acq_rel)) return;
    if (s_watchdogThread.joinable()) {
        s_watchdogThread.join();
    }
}

} // namespace nfs_audio_watchdog
