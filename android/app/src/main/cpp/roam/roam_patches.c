#define _GNU_SOURCE
/* roam_patches.c -- Hidden career races, Extra Options, and UI menu scaling patches */
#include "roam_patches.h"
#include "roam_host_android.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <math.h>
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

/* =========================================================================
 * Career Garage 10-Slot & Trade Car Refund (nfsu2-web parity)
 * ========================================================================= */
#define FEDB            0x0083a9d0u   /* FEDatabase */
#define G_CAREER        0x0083841cu   /* Career Stage Records */
#define CARS_SZ         0xab4cu       /* player 2's follow */
#define C_SLOTS         0x5954u       /* the career's 5 */
#define REC_SZ          0x7f8u
#define R_SPENT         0x7f0u
#define R_LOCKED        0x7f4u
#define R_OWNED         0x7f5u

static uint32_t s_last_spent[5] = {0};
static uint8_t  s_last_owned[5] = {0};

void garage_frame_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    if (!rh_mem_committed(FEDB, 4)) return;

    uint32_t db = FEDB + 0x3c0u; // G_FECARS
    if (!rh_mem_committed(db + C_SLOTS, 5 * REC_SZ)) return;

    // 1. Stage-based garage slot unlock:
    // Stage 0: 1 slot, Stage 1: 2 slots, Stage 2: 3 slots, Stage 3: 4 slots, Stage 4+: 5 slots
    uint32_t stage = 0;
    if (rh_mem_committed(0x007f6de8u, 4)) {
        uint32_t st = MR32(0x007f6de8u);
        if (st < 5) stage = st;
    }
    uint32_t allowed_slots = stage + 1;
    if (allowed_slots > 5) allowed_slots = 5;

    for (uint32_t i = 0; i < allowed_slots; i++) {
        uint32_t r = db + C_SLOTS + i * REC_SZ;
        if (MR8(r + R_LOCKED) != 0) {
            MR8(r + R_LOCKED) = 0;
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-Garage", "Unlocked Career Garage slot %u (stage %u)", i + 1, stage);
        }
    }

    // 2. Trade Car: 80% parts refund when trading in / replacing a customized car
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t r = db + C_SLOTS + i * REC_SZ;
        uint8_t owned = MR8(r + R_OWNED);
        uint32_t spent = MR32(r + R_SPENT);

        if (s_last_owned[i] && !owned && s_last_spent[i] > 0) {
            uint32_t refund = (s_last_spent[i] * 80u) / 100u;
            if (rh_mem_committed(0x0083a9e0u + 0x74u, 4)) {
                MR32(0x0083a9e0u + 0x74u) += refund;
            }
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-Garage",
                                "Trade Car: slot %u car traded in, refunded %u (80%% of %u) to bank",
                                i + 1, refund, s_last_spent[i]);
            s_last_spent[i] = 0;
        } else if (owned) {
            s_last_spent[i] = spent;
        }
        s_last_owned[i] = owned;
    }
}

/* =========================================================================
 * FEng UI Level Sorter Expansion (512 -> 8192 nodes, nfsu2-web parity)
 * ========================================================================= */
#define FE_NODES_ADDR  0x00838494u
#define FE_CAP_ADDR    0x0083848cu
#define FE_SCRATCH_VA  0x003A0000u
#define FE_BIG_NODES   8192u

void feng_ui_expand_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    if (!rh_mem_committed(FE_CAP_ADDR, 12)) return;

    if (MR32(FE_CAP_ADDR) < FE_BIG_NODES) {
        if (rh_mem_committed(FE_SCRATCH_VA, FE_BIG_NODES * 8u)) {
            MR32(FE_NODES_ADDR) = FE_SCRATCH_VA;
            MR32(FE_CAP_ADDR) = FE_BIG_NODES;
        }
    }
}

/* =========================================================================
 * Menu Fixes & Performance Shop Uninitialized Field Sanitizer (overrides.c parity)
 * ========================================================================= */
void menu_fixes_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;

    // 1. Sanitize uninitialized field +0xC0 on UI_ChoosePerformancePackage (VT_PERF_PACKAGE = 0x0079D2D0)
    // Prevents performance shop category navigation lockups and freeze on ESC
    uint32_t active_screen = rh_mem_committed(0x00838480u, 4) ? MR32(0x00838480u) : 0;
    if (active_screen && rh_mem_committed(active_screen, 0x100)) {
        uint32_t vtable = MR32(active_screen);
        if (vtable == 0x0079D2D0u || vtable == 0x0079D2B8u) {
            if (MR32(active_screen + 0xc0u) != 0xEEEEEEEEu) {
                MR32(active_screen + 0xc0u) = 0xEEEEEEEEu;
            }
        }
    }

    // 2. Memory card / Profile name fallback: ensure profile name at 0x0083a9e0 is non-empty
    if (rh_mem_committed(0x0083a9e0u, 32)) {
        if (MR8(0x0083a9e0u) == 0 && MR8(0x0083a9e0u + 0x20u) != 0) {
            memcpy(GPTR(0x0083a9e0u), GPTR(0x0083a9e0u + 0x20u), 16);
        }
    }
}

/* =========================================================================
 * Sound Lead Window Tick Reduction (sndlead.c parity)
 * ========================================================================= */
void sndlead_frame_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    // Shorten sound window ticks from 3000 to 300 so audio latency recovers
    // in 3 seconds after loading screens instead of 30 seconds.
    if (rh_mem_committed(0x008183e4u, 2)) {
        if (MR16(0x008183e4u) > 300) {
            MR16(0x008183e4u) = 300;
        }
    }
}

/* =========================================================================
 * Gearbox Transmission Whine System (whine.c parity)
 * ========================================================================= */
static float s_whine_pitch = 0.3f;
static float s_whine_volume = 0.0f;

void whine_frame_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    if (!rh_mem_committed(0x008900acu, 4)) return; // _PlayersByNumber[0]

    uint32_t pl = MR32(0x008900acu);
    if (!pl || !rh_mem_committed(pl + 4, 4)) return;

    uint32_t car = MR32(pl + 4);
    if (!car || !rh_mem_committed(car + 0x430u, 8)) return;

    // car + 0x430: speed in m/s, times 3.6: km/h
    float speed_ms = MRF32(car + 0x430u);
    float kmh = fabsf(speed_ms) * 3.6f;
    uint32_t gear = MR32(car + 0x434u); // 0 reverse, 1 neutral, 2 first...

    // Game flow state: 6 = in world / racing
    uint32_t flow = rh_mem_committed(0x008654a4u, 4) ? MR32(0x008654a4u) : 0;
    if (flow != 6) {
        s_whine_volume = 0.0f;
        return;
    }

    if (kmh < 1.0f) {
        s_whine_pitch = 0.3f;
        s_whine_volume = 0.0f;
    } else if (gear == 0) {
        s_whine_pitch = 0.3f + 0.7f * (kmh / 80.0f);
        s_whine_volume = 0.050f;
    } else {
        float gear_offset = (gear >= 2) ? (float)(gear - 2) * 0.02f : 0.0f;
        float sub = sqrtf(kmh / 70.0f) * 0.5f;
        if (sub > 0.6f) sub = 0.6f;
        s_whine_pitch = 0.3f + (0.7f * kmh / 330.0f) + sub + gear_offset;
        s_whine_volume = 0.030f + 0.015f * (kmh / 140.0f);
        if (s_whine_volume > 0.075f) s_whine_volume = 0.075f;
    }
}

/* =========================================================================
 * 3600 Hz Exact Simulation Tick Base (framerate.c parity)
 * ========================================================================= */
#define A_TICK_HZ_ADDR  0x007a061cu   /* 1800.0 vanilla -> 3600.0 exact */
#define A_TICK_4K_ADDR  0x007a0aa8u   /* 4000/1800 vanilla -> 4000/3600 exact */
#define A_TICK_S_ADDR   0x007a0618u   /* 1/1800 vanilla -> 1/3600 exact */

void framerate_tick_base_apply(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    if (!rh_mem_committed(A_TICK_S_ADDR, 8) || !rh_mem_committed(A_TICK_4K_ADDR, 4)) return;

    if (MRF32(A_TICK_HZ_ADDR) != 3600.0f) {
        MRF32(A_TICK_HZ_ADDR) = 3600.0f;
        MRF32(A_TICK_4K_ADDR) = (float)(4000.0 / 3600.0);
        MRF32(A_TICK_S_ADDR)  = (float)(1.0 / 3600.0);
    }
}
