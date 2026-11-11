/*
 * overrides.c - guest functions replaced by hand-written C.
 *
 * Every entry needs a reason that is a fact about the hardware or the host,
 * not "it crashed": an override hides whatever the original did.
 *
 * Calling convention inside the lifted model: the caller has pushed a return
 * slot, arguments are at g_esp+4.., the body leaves its result in g_eax and
 * pops what `ret N` would (g_esp += 4 + N).
 */
#include <string.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#include "recomp_types.h"
#include "nfs_runtime.h"

/* 0x006F5FC5: Cyrix detection.
 *   xor eax,eax; sahf; mov eax,5; mov ebx,2; div bl; lahf; cmp ah,2; ...
 * Cyrix parts leave the flags untouched across DIV, so AH reads back 2 (the
 * value SAHF loaded, plus the always-set bit 1); Intel/AMD do not. DIV's flags
 * are architecturally undefined and the lifter does not model them, so the
 * lifted probe always answers "Cyrix". The host is never a Cyrix: return 0. */
static void ov_is_cyrix(void) {
    g_eax = 0;
    g_esp += 4;
}

/* 0x0075CFD0: the static CRT memcpy/memmove(dst, src, n) (cdecl, returns dst).
 * It handles overlap by copying backward, and reaches its unrolled dword moves
 * through computed jumps into the middle of straight-line blocks (tables at
 * 0x75D11C, 0x75D268, 0x75D2B8...). Not every target is a lifted entry, so an
 * overlapping move with 8+ dwords stopped at "ITAIL unresolved 0x0075D27C".
 * Its contract is exactly the host memmove over guest memory. */
static void ov_crt_memmove(void) {
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (n) memmove((void *)ADDR(dst), (const void *)ADDR(src), n);
    g_eax = dst;
    g_esp += 4;
}

/* 0x005C68D0: Bleach Bypass post-processing pass.
 * Original PC code executes a 3-pass ping-pong render target pipeline across
 * full-screen surfaces and calls CPU LockRect, saturating memory bandwidth on mobile
 * TBDR GPUs and dropping framerate from 60 to 40-45 FPS (Issue #70).
 * Replaced with zero-bandwidth hardware tone regulation (SetGammaRamp S-curve),
 * maintaining the full Enhanced Contrast aesthetic with solid 60 FPS. */
static void ov_bleach_bypass(void) {
#if !defined(_WIN32)
    static void (*s_contrast_tone)(int) = NULL;
    if (!s_contrast_tone) {
        void *h = dlopen("libgraphics_cache.so", RTLD_NOW | RTLD_LOCAL);
        s_contrast_tone = h ? (void (*)(int))dlsym(h, "nfs_contrast_apply_hardware_tone") : NULL;
        if (!s_contrast_tone) s_contrast_tone = (void (*)(int))dlsym(RTLD_DEFAULT, "nfs_contrast_apply_hardware_tone");
    }
    if (s_contrast_tone) s_contrast_tone(1);
#endif
    if (MEM32(0x008708F4u) != 0xFFFFFFFFu) {
        MEM32(0x008708F4u) = 0xFFFFFFFFu;
    }
    g_esp += 4;
}

static const struct { uint32_t va; nfs_override_fn fn; } k_overrides[] = {
    { 0x005C68D0u, ov_bleach_bypass },
    { 0x006F5FC5u, ov_is_cyrix },
    { 0x0075CFD0u, ov_crt_memmove },
};

nfs_override_fn nfs_override_lookup(uint32_t va) {
    for (unsigned i = 0; i < sizeof k_overrides / sizeof k_overrides[0]; i++)
        if (k_overrides[i].va == va) return k_overrides[i].fn;
    return 0;
}

int nfs_override_count(void) { return (int)(sizeof k_overrides / sizeof k_overrides[0]); }
