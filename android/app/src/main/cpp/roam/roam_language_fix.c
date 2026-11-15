#define _GNU_SOURCE
#include "roam_language_fix.h"

#include <android/log.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TAG "NFSU2-Language"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

static volatile uintptr_t *s_p_mem_base = NULL;
static int (*s_p_guest_is_readable)(uint32_t va, uint32_t bytes) = NULL;

static inline void *lang_gptr(uint32_t guest_va) {
    if (!s_p_mem_base || !*s_p_mem_base) return NULL;
    return (void *)(*s_p_mem_base + (uintptr_t)guest_va);
}

static inline uint32_t lang_mr32(uint32_t guest_va) {
    void *p = lang_gptr(guest_va);
    return p ? *(volatile uint32_t *)p : 0;
}

static inline void lang_mw32(uint32_t guest_va, uint32_t val) {
    void *p = lang_gptr(guest_va);
    if (p) *(volatile uint32_t *)p = val;
}

static inline uint16_t lang_mr16(uint32_t guest_va) {
    void *p = lang_gptr(guest_va);
    return p ? *(volatile uint16_t *)p : 0;
}

static inline void lang_mw16(uint32_t guest_va, uint16_t val) {
    void *p = lang_gptr(guest_va);
    if (p) *(volatile uint16_t *)p = val;
}

static inline int lang_is_committed(uint32_t guest_va, size_t sz) {
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

void roam_language_fix_init(void) {
    if (!s_p_mem_base) {
        s_p_mem_base = (volatile uintptr_t *)dlsym(RTLD_DEFAULT, "g_mem_base");
    }
    if (!s_p_mem_base || !*s_p_mem_base) return;

    // 1. Expand Latin Font 0 extra headroom at 0x007f6de8 (FONT_SIZES)
    // Sizing LanguageMemoryPool (0x4ff680) from 273 KB to ~940 KB (+512 KB),
    // allowing Spanish (280 KB), French (287 KB), German (284 KB) to fit (Issue #112).
    if (lang_is_committed(0x007f6de8u, 8)) {
        if (lang_mr32(0x007f6de8u) == 0x5b9d88b9u) {
            uint32_t curr_sz = lang_mr32(0x007f6decu);
            if (curr_sz < 524288u) {
                lang_mw32(0x007f6decu, 524288u);
                LOGI("roam_language_fix: Expanded LanguageMemoryPool headroom to 512KB at 0x7f6dec (was %u)", curr_sz);
            }
        }
    }

    // 2. Safety patch for capacity branch in LoadLanguage (0x004ff85b)
    // In SPEED2.EXE at 0x004ff85b: 7c 16 (jl 0x4ff873)
    // NOP out the conditional jump so LoadLanguage unconditionally assigns stream pool ID 5.
    if (lang_is_committed(0x004ff85bu, 2)) {
        if (lang_mr16(0x004ff85bu) == 0x167cu) {
            lang_mw16(0x004ff85bu, 0x9090u);
            LOGI("roam_language_fix: Patched 0x004ff85b to NOPs for unconditional pool 5 assignment");
        }
    }
}

void roam_language_fix_tick(void) {
    if (!s_p_mem_base || !*s_p_mem_base) {
        s_p_mem_base = (volatile uintptr_t *)dlsym(RTLD_DEFAULT, "g_mem_base");
        if (!s_p_mem_base || !*s_p_mem_base) return;
    }

    // Ensure headroom stays applied
    if (lang_is_committed(0x007f6de8u, 8)) {
        if (lang_mr32(0x007f6de8u) == 0x5b9d88b9u && lang_mr32(0x007f6decu) < 524288u) {
            lang_mw32(0x007f6decu, 524288u);
        }
    }

    // Ensure capacity branch stays patched
    if (lang_is_committed(0x004ff85bu, 2)) {
        if (lang_mr16(0x004ff85bu) == 0x167cu) {
            lang_mw16(0x004ff85bu, 0x9090u);
        }
    }
}
