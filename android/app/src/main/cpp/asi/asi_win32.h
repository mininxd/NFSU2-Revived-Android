#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thunk virtual address ranges inside guest 32-bit address space */
#define ASI_THUNK_BASE      0xF1000000u
#define ASI_THUNK_STRIDE    4u
#define ASI_THUNK_MAX       2048u
#define ASI_RET_SENTINEL    0xFE000000u

/* Dedicated guest memory arenas (all inside 4 GiB g_mem_base reservation) */
#define ASI_IMAGE_ARENA     0x30000000u   /* 32 MiB for mapped .asi PE32 images */
#define ASI_IMAGE_ARENA_SZ  0x02000000u
#define ASI_HEAP_ARENA      0x34000000u   /* 32 MiB private heap for .asi malloc/HeapAlloc */
#define ASI_HEAP_ARENA_SZ   0x02000000u
#define ASI_STACK_BASE      0x36000000u   /* 1 MiB stack for .asi DllMain/InitializeASI */
#define ASI_STACK_SIZE      0x00100000u
#define ASI_TEB_BASE        0x36100000u   /* 64 KiB TEB/PEB + TLS slots */
#define ASI_TEB_SIZE        0x00010000u

typedef struct {
    char     dll[64];
    char     name[96];
    uint32_t thunk_va;
    int      resolved;
    int      logged_unimpl;
} AsiThunkEntry;

typedef struct AsiLoadedModInfo {
    char     name[128];
    char     host_path[1024];
    uint32_t base_va;
    uint32_t image_size;
    uint32_t entry_va;
    uint32_t init_asi_va;
    int      active;
} AsiLoadedModInfo;

/* Callbacks provided by asi_loader.c to asi_win32.c */
typedef struct {
    uint8_t *mem_base;
    const char *game_dir;
    uint32_t (*resolve_mod_handle)(const char *name);
    const AsiLoadedModInfo *(*find_mod_by_handle)(uint32_t hmod);
    const AsiLoadedModInfo *(*find_mod_by_addr)(uint32_t va);
    uint32_t (*resolve_proc_addr)(uint32_t hmod, const char *name);
    uint32_t (*load_library)(const char *name);
    void     (*notify_virtual_protect)(uint32_t va, uint32_t size, uint32_t fl_new);
    int      (*spawn_asi_thread)(uint32_t start_va, uint32_t param_va);
} AsiWin32Host;

/* Initialize private guest heap, Win32 handle tables, and UCRT shims */
void asi_win32_init(const AsiWin32Host *host);

/* Allocate/free from the isolated guest ASI heap (returns guest VA) */
uint32_t asi_heap_alloc(uint32_t size, int zero_fill);
uint32_t asi_heap_realloc(uint32_t va, uint32_t new_size);
void     asi_heap_free(uint32_t va);
uint32_t asi_heap_size(uint32_t va);

/* Register or reuse an IAT thunk VA for dll!name */
uint32_t asi_win32_register_thunk(const char *dll, const char *name, int *out_resolved);

/* Dispatch a Win32/UCRT thunk call when Unicorn hits EIP == thunk_va.
 * Reads/writes guest stack & registers via the provided pointers and returns the new EIP. */
typedef struct {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    double   st0;
    int      has_st0;
} AsiCallRegs;

uint32_t asi_win32_dispatch(uint32_t thunk_va, AsiCallRegs *regs);

#ifdef __cplusplus
}
#endif
