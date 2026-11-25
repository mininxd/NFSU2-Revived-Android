#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*asi_recomp_fn_t)(void);
typedef void *(*asi_recomp_lookup_fn_t)(uint32_t va);
typedef asi_recomp_fn_t (*asi_inline_hook_fn_t)(void *target_func, void *hook_func);
typedef int (*asi_mem_committed_fn_t)(uint32_t va, uint32_t len);

/* Runtime pointers passed from roam_android.c into libnfsu2_asi.so */
typedef struct {
    const char *game_dir;
    uintptr_t   lib_base;
    uint8_t   **p_mem_base;
    uint32_t   *p_eax;
    uint32_t   *p_ecx;
    uint32_t   *p_edx;
    uint32_t   *p_ebx;
    uint32_t   *p_esp;
    uint32_t   *p_ebp;
    uint32_t   *p_esi;
    uint32_t   *p_edi;
    int32_t    *p_fp_top;
    double     *p_st;         /* double g_st[8] */
    uint32_t   *p_fs_base;
    asi_recomp_lookup_fn_t recomp_lookup;
    asi_inline_hook_fn_t   arm64_inline_hook;
    asi_mem_committed_fn_t mem_committed;
    /* Sorted lifted function table in libnfsu2_android.so (va, pad, arm64_fn) */
    const void *recomp_table;
    uint32_t    recomp_table_count;
} AsiHostBridge;

#define ASI_MOD_MAX 32

typedef struct {
    char name[128];       /* filename e.g. BayviewForever.asi */
    int  builtin;         /* 1 if WidescreenFix / FreeRoam (auto-skipped unless user forces ON) */
    int  enabled_cfg;     /* persisted setting in scripts/modloader.cfg */
    int  loaded_now;      /* 1 if actively loaded in this session */
    int  patched_funcs;   /* number of SPEED2.EXE functions detoured to Unicorn */
    int  failed;          /* 1 if load/init threw an error */
} AsiModListItem;

/* Boot entry point called by roam_android.c inside nfs_widescreen_init when ASI Loader is ON */
int asi_loader_boot(const AsiHostBridge *bridge);

/* Query active loaded status for the in-game Modloader UI */
int asi_loader_is_active(void);
int asi_loader_get_loaded_summary(char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
