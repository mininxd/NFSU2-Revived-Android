#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>

extern "C" {
int nfs_extra_options_init(const char* game_root, int dumb_ai_cheat, int starting_cash_cheat);
size_t nfs_extra_options_patch_count();
const uint8_t* nfs_extra_options_get_patch(size_t index, uint32_t* out_va, size_t* out_len);
int nfs_extra_options_is_dumb_ai_enabled();
void nfs_extra_options_set_dumb_ai_enabled(int enabled);
}

static inline void applyExtraOptions(const char* gameRoot) {
    const char* dumbEnv = std::getenv("NFSU2_DUMB_AI");
    int dumbAiCheat = (dumbEnv && std::strcmp(dumbEnv, "1") == 0) ? 1 : 0;

    const char* cashEnv = std::getenv("NFSU2_STARTING_CASH");
    int cashCheat = (cashEnv && std::strcmp(cashEnv, "1") == 0) ? 1 : 0;

    int patchCount = nfs_extra_options_init(gameRoot, dumbAiCheat, cashCheat);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Extra Options: loaded settings, applying %d memory patches", patchCount);

    size_t total = nfs_extra_options_patch_count();
    size_t applied = 0;
    for (size_t i = 0; i < total; ++i) {
        uint32_t va = 0;
        size_t len = 0;
        const uint8_t* patchBytes = nfs_extra_options_get_patch(i, &va, &len);
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
                    applied, total, nfs_extra_options_is_dumb_ai_enabled());
}

static inline void updateDumbAIState() {
    if (!memory) return;
    int enabled = nfs_extra_options_is_dumb_ai_enabled();
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
