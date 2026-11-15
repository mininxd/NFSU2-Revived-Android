#define _GNU_SOURCE
#include "roam_nitro_fix.h"

#include <android/log.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAG "NFSU2-Nitro"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

static volatile uintptr_t *s_p_mem_base = NULL;
static int (*s_p_guest_is_readable)(uint32_t va, uint32_t bytes) = NULL;

static inline void *nitro_gptr(uint32_t guest_va) {
    if (!s_p_mem_base) {
        s_p_mem_base = (volatile uintptr_t *)dlsym(RTLD_DEFAULT, "g_mem_base");
    }
    if (!s_p_mem_base || !*s_p_mem_base) return NULL;
    return (void *)(*s_p_mem_base + (uintptr_t)guest_va);
}

static inline uint32_t nitro_mr32(uint32_t guest_va) {
    void *p = nitro_gptr(guest_va);
    return p ? *(volatile uint32_t *)p : 0;
}

static inline float nitro_mrf32(uint32_t guest_va) {
    void *p = nitro_gptr(guest_va);
    return p ? *(volatile float *)p : 0.0f;
}

static inline int nitro_is_readable(uint32_t guest_va, size_t sz) {
    if (!s_p_mem_base) {
        s_p_mem_base = (volatile uintptr_t *)dlsym(RTLD_DEFAULT, "g_mem_base");
    }
    if (!s_p_mem_base || !*s_p_mem_base) return 0;
    if (guest_va < 0x400000u || (uint64_t)guest_va + sz >= 0x80000000ULL) return 0;
    if (!s_p_guest_is_readable) {
        s_p_guest_is_readable = (int (*)(uint32_t, uint32_t))dlsym(RTLD_DEFAULT, "guest_is_readable");
    }
    if (s_p_guest_is_readable) {
        return s_p_guest_is_readable(guest_va, (uint32_t)sz);
    }
    return 1;
}

static uint32_t nitro_get_player_car(void) {
    if (!nitro_is_readable(0x008900acu, 4)) return 0; // SPLIT_PLAYERS_INDEX
    uint32_t pl = nitro_mr32(0x008900acu);
    if (!pl || pl < 0x400000u || pl >= 0x7fffffffu || !nitro_is_readable(pl + 4u, 4)) return 0;
    uint32_t car = nitro_mr32(pl + 4u);
    if (!car || car < 0x400000u || car >= 0x7fffffffu || !nitro_is_readable(car + 0x60u, 16)) return 0;
    return car;
}

float roam_nitro_get_car_speed_kmh(void) {
    uint32_t car = nitro_get_player_car();
    if (!car) return 0.0f;

    // 1. Try vehicle physics speed at car + 0x430 (m/s)
    if (nitro_is_readable(car + 0x430u, 4)) {
        float spd = nitro_mrf32(car + 0x430u);
        if (isfinite(spd) && spd >= 0.0f && spd < 200.0f) {
            return spd * 3.6f;
        }
    }

    // 2. Try speedometer speed at car + 0x42c (m/s)
    if (nitro_is_readable(car + 0x42cu, 4)) {
        float spd = nitro_mrf32(car + 0x42cu);
        if (isfinite(spd) && spd >= 0.0f && spd < 200.0f) {
            return spd * 3.6f;
        }
    }

    // 3. Fallback: linear velocity vector (vx, vy, vz) magnitude at car + 0x70
    if (nitro_is_readable(car + 0x70u, 12)) {
        float vx = nitro_mrf32(car + 0x70u);
        float vy = nitro_mrf32(car + 0x74u);
        float vz = nitro_mrf32(car + 0x78u);
        if (isfinite(vx) && isfinite(vy) && isfinite(vz)) {
            float len = sqrtf(vx * vx + vy * vy + vz * vz);
            if (isfinite(len) && len >= 0.0f && len < 200.0f) {
                return len * 3.6f;
            }
        }
    }

    return 0.0f;
}

int roam_nitro_get_car_nos_percent(void) {
    uint32_t car = nitro_get_player_car();
    if (!car) return 0;
    uint32_t n = car + 0x40u;
    if (!nitro_is_readable(n + 0x414u, 16)) return 0;
    uint32_t full = nitro_mr32(n + 0x414u);
    uint32_t left = nitro_mr32(n + 0x41cu);
    if (full == 0) return 0;
    return (int)((left * 100u) / full);
}

int roam_nitro_is_nos_active(void) {
    uint32_t car = nitro_get_player_car();
    if (!car) return 0;
    uint32_t n = car + 0x40u;
    if (!nitro_is_readable(n + 0x420u, 4)) return 0;
    return (nitro_mr32(n + 0x420u) != 0) ? 1 : 0;
}
