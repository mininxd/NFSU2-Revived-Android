#define _GNU_SOURCE
/* roam_patches.c -- Hidden career races, Extra Options, and UI menu scaling patches */
#include "roam_patches.h"
#include "roam_host_android.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <android/log.h>

/* Hidden Races Mod: unhides 33 career races & raises stage caps to 8 */
#define G_RACES         0x008383e0u
#define G_NRACES        0x008383e4u
#define G_STAGES        0x0083841cu
#define G_NSTAGES       0x00838418u
#define RACE_SZ         0x88u
#define STAGE_SZ        0x50u
#define R_HIDDEN        0x7fu
#define S_CAPS          0x30u
#define CAP             8

static int g_hidden_on = -1;
static uint32_t g_hidden_done = 0;

void hiddenraces_frame_tick(void) {
    if (__builtin_expect(g_hidden_done != 0, 1)) return;
    if (g_hidden_on < 0) {
        const char *e = getenv("NFSU2_SHOW_HIDDEN_RACES");
        g_hidden_on = e && *e && *e != '0' && strcasecmp(e, "off") && strcasecmp(e, "no") && strcasecmp(e, "false");
    }
    if (!g_hidden_on) return;
    if (!rh_mem_committed(G_RACES, 4) || !rh_mem_committed(G_STAGES, 4)) return;
    uint32_t races = MR32(G_RACES), nr = MR32(G_NRACES), stages = MR32(G_STAGES), ns = MR32(G_NSTAGES);
    if (!races || races == g_hidden_done || nr > 1024u || ns > 16u || !rh_mem_committed(races, nr * RACE_SZ) || !rh_mem_committed(stages, ns * STAGE_SZ))
        return;
    int n = 0, caps = 0;
    for (uint32_t i = 0; i < nr; i++) {
        if (MR8(races + i * RACE_SZ + R_HIDDEN)) {
            MR8(races + i * RACE_SZ + R_HIDDEN) = 0;
            n++;
        }
    }
    for (uint32_t s = 0; s < ns; s++) {
        for (uint32_t k = 0; k < 5u; k++) {
            uint32_t a = stages + s * STAGE_SZ + S_CAPS + k;
            int8_t v = (int8_t)MR8(a);
            if (v >= 0 && v < CAP) {
                MR8(a) = CAP;
                caps++;
            }
        }
    }
    g_hidden_done = races;
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "[career] show hidden races: %d of %u races out of hiding, %d caps raised to %d",
                        n, nr, caps, CAP);
}

/* Extra Options (ExOpts Team) Memory Patches & Dumb AI (AutoPilot) Hack */
void extraoptions_init_patches(void) {
    if (!rh_mem_committed(0x00400000u, 0x1000)) return;

    // Bleach Bypass / Enhanced Contrast engine resolve clamp:
    // Clamp 0x00870D00 (g_BleachByPassEnable) and 0x00870CFC (g_OverBrightEnable) to 0
    // so SPEED2.EXE skips slow 3-pass ping-pong resolves that drop FPS to 30-40.
    if (rh_mem_committed(0x00870D00u, 4)) {
        MR32(0x00870D00u) = 0;
    }
    if (rh_mem_committed(0x00870CFCu, 4)) {
        MR32(0x00870CFCu) = 0;
    }

    // Optical drive / Disc 2 bypass
    if (rh_mem_committed(0x005C0D3Fu, 1)) MR8(0x005C0D3Fu) = 0x78;
    if (rh_mem_committed(0x005C0D54u, 1)) MR8(0x005C0D54u) = 0x00;

    // Ground Shadow Alignment Fix (0.015f instead of 0.04f to sit flush with road)
    if (rh_mem_committed(0x007A66B4u, 4)) MRF32(0x007A66B4u) = 0.015f;

    // Lap Controllers (0 to 127 laps)
    if (rh_mem_committed(0x004B3D82u, 30)) {
        MR8(0x004B3D82u) = 0;   // min lap decrement
        MR8(0x004B3D9Bu) = 0;   // min lap increment
        MR8(0x004B3D86u) = 127; // max lap decrement
        MR8(0x004B3D97u) = 127; // max lap increment
    }
    if (rh_mem_committed(0x004B407Fu, 1)) MR8(0x004B407Fu) = 5; // KO laps

    // LAN Laps (0 to 127)
    if (rh_mem_committed(0x004B3E7Au, 1)) MR8(0x004B3E7Au) = 0;
    if (rh_mem_committed(0x004B3EE4u, 1)) MR8(0x004B3EE4u) = 0;
    if (rh_mem_committed(0x004B3E9Eu, 1)) MR8(0x004B3E9Eu) = 0;
    if (rh_mem_committed(0x004B3F30u, 1)) MR8(0x004B3F30u) = 127;
    if (rh_mem_committed(0x004B3F41u, 1)) MR8(0x004B3F41u) = 127;

    // Opponent Controllers (0 to 5 opponents)
    if (rh_mem_committed(0x004B382Bu, 70)) {
        MR32(0x004B382Bu) = 0x90909090u;
        MR16(0x004B382Fu) = 0x9090u;
        MR8(0x004B3834u) = 5; // Max CPU
        MR8(0x004B3863u) = 0; // Min CPU
        MR32(0x004B3867u) = 0x90909090u;
    }
    if (rh_mem_committed(0x004B4075u, 1)) MR8(0x004B4075u) = 5; // KO CPU

    // LAN Players (up to 6)
    if (rh_mem_committed(0x004B31AAu, 4)) {
        MR8(0x004B31AAu) = 0xB0;
        MR8(0x004B31ABu) = 6;
        MR16(0x004B31ACu) = 0x9090u;
    }

    // Unfreeze KO (ShowMoreRaceOptions)
    if (rh_mem_committed(0x004B4068u, 1)) MR8(0x004B4068u) = 0xEB;
    if (rh_mem_committed(0x004B3E14u, 1)) MR8(0x004B3E14u) = 0xEB;
    if (rh_mem_committed(0x004B3903u, 1)) MR8(0x004B3903u) = 0xEB;

    // Regional Cars (Peugeot 106 & Opel Corsa unlocked on NTSC)
    if (rh_mem_committed(0x007F7C48u, 16)) {
        MR32(0x007F7C48u) = 0xFFFFFFFFu; // CORSA
        MR32(0x007F7C4Cu) = 0xFFFFFFFFu; // PEUGOT106
        MR32(0x007F7C50u) = 0xFFFFFFFFu; // CIVIC
        MR32(0x007F7C54u) = 0xFFFFFFFFu; // RSX
    }
    // Quick Race car select loop limit: 0x1C (28 cars, includes 106 & Corsa)
    if (rh_mem_committed(0x004FEB9Du, 1)) {
        MR8(0x004FEB9Du) = 0x1C;
    }
    // Career mode starter cars count (adds 106 and Corsa into car lot)
    if (rh_mem_committed(0x007F6DBEu, 2)) {
        MR8(0x007F6DBEu) = 0x07;
        MR8(0x007F6DBFu) = 0x06;
    }
    // Profile regional cars unlock flag at 0x00532908: mov eax, 1 (b8 01 00 00 00)
    if (rh_mem_committed(0x00532908u, 5)) {
        MR8(0x00532908u) = 0xB8;
        MR32(0x00532909u) = 0x00000001;
    }

    // Unlock all customization parts at start
    const char *e_parts = getenv("NFSU2_UNLOCK_PARTS");
    if (e_parts && *e_parts && *e_parts != '0' && strcasecmp(e_parts, "off") && strcasecmp(e_parts, "false")) {
        if (rh_mem_committed(0x00523C64u, 6)) {
            MR8(0x00523C64u) = 0xB8;
            MR32(0x00523C65u) = 0x00000001;
            MR8(0x00523C69u) = 0xC3;
        }
    }

    // Neon pulsation / flash enhancement
    if (rh_mem_committed(0x007F35A0u, 4)) {
        MRF32(0x007F35A0u) = 1.0f;
    }
}

static int g_dumb_ai = 0;

void extraoptions_set_dumb_ai(int dumb_ai) {
    g_dumb_ai = dumb_ai;
}

void extraoptions_frame_tick(void) {
    static int s_patches_inited = 0;
    if (!s_patches_inited) {
        if (rh_mem_committed(0x00400000u, 0x1000)) {
            s_patches_inited = 1;
            extraoptions_init_patches();
        }
    }

    const char *e = getenv("NFSU2_AUTOPILOT");
    int dumb_ai = g_dumb_ai || (e && *e && *e != '0' && strcasecmp(e, "off") && strcasecmp(e, "false"));
    static int s_applied_state = -1;
    static uint32_t s_applied_player = 0;

    if (!rh_mem_committed(0x008900ACu, 4)) {
        s_applied_player = 0;
        s_applied_state = -1;
        return;
    }

    uint32_t player = MR32(0x008900ACu); // _PlayersByNumber[0]
    if (!player) {
        s_applied_player = 0;
        s_applied_state = -1;
        return;
    }

    if (s_applied_player != player || s_applied_state != dumb_ai) {
        if (dumb_ai) {
            // Player_AutoPilotOn (0x005FAE20) with ecx = player (__thiscall)
            roam_android_call(0x005FAE20u, player, 0, 0, NULL);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-ExtraOptions",
                                "Dumb AI (AutoPilot) ENABLED on player car 0x%08x", player);
        } else {
            // Player_AutoPilotOff (0x005FAF10) with ecx = player
            roam_android_call(0x005FAF10u, player, 0, 0, NULL);
            // Player_SetInputMode (0x00605C70) with ecx = player, mode = 2
            uint32_t arg = 2;
            roam_android_call(0x00605C70u, player, 0, 1, &arg);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-ExtraOptions",
                                "Dumb AI (AutoPilot) DISABLED on player car 0x%08x", player);
        }
        s_applied_player = player;
        s_applied_state = dumb_ai;
    }
}

void menusize_frame_tick(void) {
    /* 200% button sizing and per-frame UI mutation disabled to prevent
     * flickering and EventHandlerSlotPool exhaustion. */
}
