#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <dlfcn.h>
#include <android/log.h>

typedef int (*pfn_extra_init)(const char*, int, int);
typedef size_t (*pfn_extra_count)();
typedef const uint8_t* (*pfn_extra_patch)(size_t, uint32_t*, size_t*);
typedef int (*pfn_extra_dumb)();
typedef void (*pfn_extra_set_dumb)(int);

static inline void* get_graphics_cache_handle() {
    static void* handle = nullptr;
    if (!handle) {
        handle = dlopen("libgraphics_cache.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libgraphics_cache.so", RTLD_NOW | RTLD_GLOBAL);
    }
    return handle;
}

static inline void applyExtraOptions(const char* gameRoot) {
    if (!gameRoot) return;

    // Detect dinput8.dll (ASI Loader proxy) and NFSU2ExtraOptions.asi in the game root or scripts folder
    std::string dinputPath = std::string(gameRoot) + "/dinput8.dll";
    std::string asiPath1 = std::string(gameRoot) + "/scripts/NFSU2ExtraOptions.asi";
    std::string asiPath2 = std::string(gameRoot) + "/NFSU2ExtraOptions.asi";
    struct stat st;
    bool hasDinput = (stat(dinputPath.c_str(), &st) == 0);
    bool hasAsi = (stat(asiPath1.c_str(), &st) == 0 || stat(asiPath2.c_str(), &st) == 0);

    if (hasDinput || hasAsi) {
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Extra Options: Detected ASI mod bundle (%s%s) in game folder! Native proxy hook active.",
                        hasDinput ? "dinput8.dll " : "", hasAsi ? "NFSU2ExtraOptions.asi" : "");
    }

    const char* dumbEnv = std::getenv("NFSU2_DUMB_AI");
    int dumbAiCheat = (dumbEnv && std::strcmp(dumbEnv, "1") == 0) ? 1 : 0;

    const char* cashEnv = std::getenv("NFSU2_STARTING_CASH");
    int cashCheat = (cashEnv && std::strcmp(cashEnv, "1") == 0) ? 1 : 0;

    void* h = get_graphics_cache_handle();
    auto fn_init = h ? (pfn_extra_init)dlsym(h, "nfs_extra_options_init") : nullptr;
    if (!fn_init) fn_init = (pfn_extra_init)dlsym(RTLD_DEFAULT, "nfs_extra_options_init");
    auto fn_count = h ? (pfn_extra_count)dlsym(h, "nfs_extra_options_patch_count") : nullptr;
    if (!fn_count) fn_count = (pfn_extra_count)dlsym(RTLD_DEFAULT, "nfs_extra_options_patch_count");
    auto fn_patch = h ? (pfn_extra_patch)dlsym(h, "nfs_extra_options_get_patch") : nullptr;
    if (!fn_patch) fn_patch = (pfn_extra_patch)dlsym(RTLD_DEFAULT, "nfs_extra_options_get_patch");
    auto fn_dumb = h ? (pfn_extra_dumb)dlsym(h, "nfs_extra_options_is_dumb_ai_enabled") : nullptr;
    if (!fn_dumb) fn_dumb = (pfn_extra_dumb)dlsym(RTLD_DEFAULT, "nfs_extra_options_is_dumb_ai_enabled");

    if (!fn_init || !fn_count || !fn_patch) {
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN, "NFSU2", "Extra Options: libgraphics_cache symbols unavailable");
        return;
    }

    int patchCount = fn_init(gameRoot, dumbAiCheat, cashCheat);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Extra Options: loaded settings, applying %d memory patches", patchCount);

    size_t total = fn_count();
    size_t applied = 0;
    for (size_t i = 0; i < total; ++i) {
        uint32_t va = 0;
        size_t len = 0;
        const uint8_t* patchBytes = fn_patch(i, &va, &len);
        if (patchBytes && va != 0 && len > 0) {
            if (memory && memory->writable(va, len)) {
                void* dst = ptr(va, len);
                if (dst) {
                    std::memcpy(dst, patchBytes, len);
                    applied++;
                }
            } else {
                NFS_RUNTIME_LOG(ANDROID_LOG_WARN, "NFSU2", "Extra Options: patch at 0x%08x (%zu bytes) not writable", va, len);
            }
        }
    }
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Extra Options: successfully applied %zu/%zu patches (Dumb AI=%d)",
                    applied, total, fn_dumb ? fn_dumb() : 0);
}

static inline void updateDumbAIState() {
    if (!memory) return;
    static auto fn_dumb = []() -> pfn_extra_dumb {
        void* h = get_graphics_cache_handle();
        auto fn = h ? (pfn_extra_dumb)dlsym(h, "nfs_extra_options_is_dumb_ai_enabled") : nullptr;
        if (!fn) fn = (pfn_extra_dumb)dlsym(RTLD_DEFAULT, "nfs_extra_options_is_dumb_ai_enabled");
        return fn;
    }();
    int enabled = fn_dumb ? fn_dumb() : 0;
    // TheGameFlowManager at 0x008654A4 (6 = Gameplay)
    if (!memory->readable(0x008654A4u, 4) || !memory->readable(0x008900ACu, 4)) return;
    uint32_t flow = read32(0x008654A4u);

    static uint32_t s_appliedPlayer = 0;
    static int s_appliedState = -1;

    if (flow != 6) {
        s_appliedPlayer = 0;
        s_appliedState = -1;
        return;
    }

    uint32_t playerThing = read32(0x008900ACu);
    if (!playerThing) {
        s_appliedPlayer = 0;
        s_appliedState = -1;
        return;
    }

    if (s_appliedPlayer != playerThing || s_appliedState != enabled) {
        try {
            if (enabled) {
                // Call Player_AutoPilotOn (0x005FAE20) with ecx = playerThing (__thiscall)
                g_ecx = playerThing;
                callGuest(0x005FAE20u, {});
                NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Extra Options: Dumb AI (AutoPilot) ENABLED for player 0x%08x", playerThing);
            } else {
                // Call Player_AutoPilotOff (0x005FAF10) with ecx = playerThing
                g_ecx = playerThing;
                callGuest(0x005FAF10u, {});
                // Call Player_SetInputMode (0x00605C70) with ecx = playerThing, mode = 2
                g_ecx = playerThing;
                callGuest(0x00605C70u, {2});
                NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Extra Options: Dumb AI (AutoPilot) DISABLED for player 0x%08x", playerThing);
            }
            s_appliedPlayer = playerThing;
            s_appliedState = enabled;
        } catch (...) {
            NFS_RUNTIME_LOG(ANDROID_LOG_WARN, "NFSU2", "Extra Options: Dumb AI callGuest threw exception");
        }
    }
}
