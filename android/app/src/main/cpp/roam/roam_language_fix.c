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

extern ptrdiff_t *g_p_mem_base;
extern int roam_android_init_symbols(void);
extern int roam_android_mem_readable(uint32_t a, uint32_t n);

static inline void *lang_gptr(uint32_t guest_va) {
    if (!g_p_mem_base || !*g_p_mem_base) {
        roam_android_init_symbols();
    }
    if (!g_p_mem_base || !*g_p_mem_base) return NULL;
    return (void *)((uintptr_t)*g_p_mem_base + (uintptr_t)guest_va);
}

static inline uint32_t lang_mr32(uint32_t guest_va) {
    void *p = lang_gptr(guest_va);
    return p ? *(volatile uint32_t *)p : 0;
}

static inline void lang_mw32(uint32_t guest_va, uint32_t val) {
    void *p = lang_gptr(guest_va);
    if (p) *(volatile uint32_t *)p = val;
}

static inline int lang_is_committed(uint32_t guest_va, size_t sz) {
    if (!g_p_mem_base || !*g_p_mem_base) {
        roam_android_init_symbols();
    }
    if (!g_p_mem_base || !*g_p_mem_base) return 0;
    if (guest_va < 0x400000u || (uint64_t)guest_va + sz >= 0x80000000ULL) return 0;
    return roam_android_mem_readable(guest_va, (uint32_t)sz);
}

void roam_language_fix_init(void) {
    if (!g_p_mem_base || !*g_p_mem_base) {
        roam_android_init_symbols();
    }
    if (!g_p_mem_base || !*g_p_mem_base) return;

    // Expand Latin Font 0 extra headroom at 0x007f6de8 (FONT_SIZES[0].size at 0x007f6dec)
    // Sizing LanguageMemoryPool (0x4ff680) from 273 KB to ~940 KB (+512 KB),
    // allowing Spanish (280 KB), French (287 KB), German (284 KB) to fit without pool overflow.
    if (lang_is_committed(0x007f6de8u, 8)) {
        if (lang_mr32(0x007f6de8u) == 0x5b9d88b9u) {
            uint32_t curr_sz = lang_mr32(0x007f6decu);
            if (curr_sz < 524288u) {
                lang_mw32(0x007f6decu, 524288u);
                LOGI("roam_language_fix: Expanded LanguageMemoryPool headroom to 512KB at 0x7f6dec (was %u)", curr_sz);
            }
        }
    }
}

void roam_language_fix_tick(void) {
    // No-op during ticks to ensure pristine game loop execution
}
