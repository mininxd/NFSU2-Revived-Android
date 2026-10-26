#ifndef ROAM_HOST_ANDROID_H
#define ROAM_HOST_ANDROID_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <dlfcn.h>
#include <android/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CPU CPU; /* Dummy pointer type for guest CPU */

/* Access to guest memory */
extern ptrdiff_t *g_p_mem_base;
extern uint32_t  *g_p_esp;
extern uint32_t  *g_p_eax;
extern uint32_t  *g_p_ecx;
extern uint32_t  *g_p_edx;
typedef void (*recomp_func_t)(void);
extern recomp_func_t (*g_p_recomp_lookup)(uint32_t va);

int roam_android_init_symbols(void);

#define GPTR(a) ((uint8_t *)((*g_p_mem_base) + (uint32_t)(a)))
#define MR8(a)   (*(uint8_t  *)GPTR(a))
#define MR16(a)  (*(uint16_t *)GPTR(a))
#define MR32(a)  (*(uint32_t *)GPTR(a))
#define MRF32(a) (*(float    *)GPTR(a))

static inline int rh_committed(uint32_t a) {
    return g_p_mem_base != NULL && *g_p_mem_base != 0 && a >= 0x00010000u;
}

static inline uint32_t roam_android_call(uint32_t fn, uint32_t ecx, uint32_t edx, int n, const uint32_t *args) {
    if (!roam_android_init_symbols() || !g_p_mem_base || !*g_p_mem_base || !g_p_recomp_lookup || !g_p_esp) return 0;
    recomp_func_t target = g_p_recomp_lookup(fn);
    if (!target) return 0;

    uint32_t saved_esp = *g_p_esp;
    uint32_t saved_ecx = *g_p_ecx;
    uint32_t saved_edx = *g_p_edx;

    for (int i = n - 1; i >= 0; i--) {
        *g_p_esp -= 4;
        MR32(*g_p_esp) = args[i];
    }
    *g_p_esp -= 4;
    MR32(*g_p_esp) = 0x00581475u; /* standard return hook */

    *g_p_ecx = ecx;
    *g_p_edx = edx;

    target();

    uint32_t result = *g_p_eax;

    *g_p_esp = saved_esp;
    *g_p_ecx = saved_ecx;
    *g_p_edx = saved_edx;

    return result;
}

#define rh_call(c, fn, ecx, edx, n, args) ((void)(c), roam_android_call((fn), (ecx), (edx), (n), (args)))

/* Options, logging, clocks */
const char *roam_opt(const char *name);
void roam_log(const char *fmt, ...);
#define rlog(...) roam_log(__VA_ARGS__)
#define rh_opt(k) roam_opt(k)

long roam_android_frame_no(void);
#define rh_frame() roam_android_frame_no()
#define rh_heap_mb() 0u
#define rh_hud_marker(x, y) ((void)(x), (void)(y))
#define RH_FE_NODES 512

static inline uint64_t rh_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* Audio stubs & Horn Integration */
int roam_android_horn_held(void);
#define rh_horn_held(stick) ((void)(stick), roam_android_horn_held())
#define rh_horn_mine()   0
#define rh_horn_name(k)  ((void)(k), "stock")
#define rh_horn_begin(pos, fwd, vel)   ((void)(pos), (void)(fwd), (void)(vel))
#define rh_horn_sound(who, name, kind, p, v) ((void)(who), (void)(name), (void)(kind), (void)(p), (void)(v))
#define rh_horn_end()    ((void)0)
#define rh_carsfx_begin(pos, fwd, vel)    ((void)(pos), (void)(fwd), (void)(vel))
#define rh_carsfx_car(who, name, p, v, st) ((void)(who), (void)(name), (void)(p), (void)(v), (void)(st))
#define rh_carsfx_end()  ((void)0)

typedef struct rh_file rh_file;
rh_file *rh_fopen(const char *path);
int      rh_fprintf(rh_file *f, const char *fmt, ...);
void     rh_fflush(rh_file *f);

#ifdef __cplusplus
}
#endif

#endif /* ROAM_HOST_ANDROID_H */
