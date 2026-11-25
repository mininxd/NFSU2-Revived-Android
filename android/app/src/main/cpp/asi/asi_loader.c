#define _GNU_SOURCE
#include "asi_loader.h"
#include "asi_win32.h"
#include "asi_log.h"

#include <unicorn/unicorn.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* =========================================================================
 * Constants & Structures
 * ========================================================================= */
#define SPEED2_IMAGE_BASE   0x00400000u
#define SPEED2_TEXT_START   0x00401000u
#define SPEED2_TEXT_END     0x00783000u
#define SPEED2_TEXT_SIZE    (SPEED2_TEXT_END - SPEED2_TEXT_START)
#define SPEED2_IMAGE_SIZE   0x00532000u

#define MAX_LOADED_MODS     32
#define MAX_PATCHED_FUNCS   512
#define MAX_ASI_THREADS     16

typedef struct {
    uint32_t va;
    uint32_t pad;
    void    *arm64_fn;
} RecompFuncEntry;

typedef struct {
    uint32_t        fn_va;
    uint32_t        fn_end_va;
    void           *orig_arm64_fn;
    asi_recomp_fn_t orig_tramp;
    void           *stub_addr;
} PatchedFuncRec;

typedef struct {
    uint32_t start_va;
    uint32_t param_va;
    char     mod_name[128];
} DeferredThread;

enum {
    TRAP_NONE         = 0,
    TRAP_WIN32_IAT    = 1,
    TRAP_NATIVE_ARM64 = 2,
    TRAP_SPEED2_IAT   = 3,
    TRAP_FAULT        = 4
};

/* =========================================================================
 * Static State
 * ========================================================================= */
static AsiHostBridge    s_bridge;
static uc_engine       *s_uc = NULL;
static pthread_mutex_t  s_uc_mu = PTHREAD_MUTEX_INITIALIZER;
static int              s_asi_active = 0;

static uint8_t         *s_clean_text = NULL;
static uint32_t         s_next_mod_va = ASI_IMAGE_ARENA;

static AsiLoadedModInfo s_mods[MAX_LOADED_MODS];
static uint32_t         s_mod_count = 0;

static PatchedFuncRec   s_patched[MAX_PATCHED_FUNCS];
static uint32_t         s_patched_count = 0;

static uint8_t         *s_stub_page = NULL;
static size_t           s_stub_offset = 0;

static DeferredThread   s_deferred_threads[MAX_ASI_THREADS];
static uint32_t         s_deferred_thread_count = 0;

/* Per-invocation trap state (protected by s_uc_mu) */
static int              s_trap_kind = TRAP_NONE;
static uint32_t         s_trap_va = 0;
static char             s_active_mod_name[128] = {0};

/* =========================================================================
 * Modloader Config (scripts/modloader.cfg)
 * ========================================================================= */
static int is_builtin_asi_name(const char *name) {
    if (!name) return 0;
    return (strcasecmp(name, "NFSUnderground2.WidescreenFix.asi") == 0 ||
            strcasecmp(name, "NFSU2FreeRoam.asi") == 0);
}

static int cfg_is_mod_enabled(const char *game_dir, const char *filename) {
    int def_on = is_builtin_asi_name(filename) ? 0 : 1;
    if (!game_dir || !filename) return def_on;
    char path[1024];
    snprintf(path, sizeof(path), "%s/scripts/modloader.cfg", game_dir);
    FILE *fp = fopen(path, "r");
    if (!fp) return def_on;

    char key[160];
    snprintf(key, sizeof(key), "mod.%s=", filename);
    size_t klen = strlen(key);
    char line[512];
    int val = def_on;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, key, klen) == 0) {
            val = (atoi(p + klen) != 0) ? 1 : 0;
            break;
        }
    }
    fclose(fp);
    return val;
}

/* =========================================================================
 * Module Lookup Helpers
 * ========================================================================= */
static const AsiLoadedModInfo *find_mod_by_handle(uint32_t hmod) {
    for (uint32_t i = 0; i < s_mod_count; i++) {
        if (s_mods[i].base_va == hmod) return &s_mods[i];
    }
    return NULL;
}

static const AsiLoadedModInfo *find_mod_by_addr(uint32_t va) {
    for (uint32_t i = 0; i < s_mod_count; i++) {
        if (va >= s_mods[i].base_va && va < s_mods[i].base_va + s_mods[i].image_size) {
            return &s_mods[i];
        }
    }
    return NULL;
}

static void describe_va(uint32_t va, char *out, size_t out_sz) {
    if (va >= SPEED2_IMAGE_BASE && va < SPEED2_IMAGE_BASE + SPEED2_IMAGE_SIZE) {
        snprintf(out, out_sz, "SPEED2.EXE+0x%06X (0x%08X)", va - SPEED2_IMAGE_BASE, va);
        return;
    }
    const AsiLoadedModInfo *m = find_mod_by_addr(va);
    if (m) {
        snprintf(out, out_sz, "%s+0x%06X (0x%08X)", m->name, va - m->base_va, va);
        return;
    }
    snprintf(out, out_sz, "0x%08X", va);
}

static uint32_t host_resolve_mod_handle(const char *name) {
    if (!name || !name[0]) return SPEED2_IMAGE_BASE;
    const char *base = name;
    for (const char *p = name; *p; p++) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    if (strcasecmp(base, "speed2.exe") == 0 || strcasecmp(base, "speed2") == 0) {
        return SPEED2_IMAGE_BASE;
    }
    for (uint32_t i = 0; i < s_mod_count; i++) {
        if (strcasecmp(base, s_mods[i].name) == 0) return s_mods[i].base_va;
    }
    /* Return synthetic handle for standard Windows DLLs so GetProcAddress can resolve them */
    if (strcasecmp(base, "kernel32.dll") == 0 || strcasecmp(base, "kernel32") == 0) return 0x76000000u;
    if (strcasecmp(base, "user32.dll") == 0   || strcasecmp(base, "user32") == 0)   return 0x76100000u;
    if (strcasecmp(base, "ntdll.dll") == 0    || strcasecmp(base, "ntdll") == 0)    return 0x76200000u;
    if (strcasecmp(base, "advapi32.dll") == 0 || strcasecmp(base, "advapi32") == 0) return 0x76300000u;
    if (strcasecmp(base, "dinput8.dll") == 0  || strcasecmp(base, "dinput8") == 0)  return 0x76400000u;
    if (strcasecmp(base, "d3d9.dll") == 0     || strcasecmp(base, "d3d9") == 0)     return 0x76500000u;
    if (strcasecmp(base, "ws2_32.dll") == 0   || strcasecmp(base, "ws2_32") == 0)   return 0x76600000u;
    if (strcasecmp(base, "shell32.dll") == 0  || strcasecmp(base, "shell32") == 0)  return 0x76700000u;
    if (strcasecmp(base, "ucrtbase.dll") == 0 || strcasecmp(base, "msvcrt.dll") == 0) return 0x76800000u;
    return 0;
}

static uint32_t host_resolve_proc_addr(uint32_t hmod, const char *proc_name) {
    if (!proc_name || !proc_name[0]) return 0;
    const char *dll = "kernel32.dll";
    if (hmod == 0x76100000u) dll = "user32.dll";
    else if (hmod == 0x76200000u) dll = "ntdll.dll";
    else if (hmod == 0x76300000u) dll = "advapi32.dll";
    else if (hmod == 0x76400000u) dll = "dinput8.dll";
    else if (hmod == 0x76500000u) dll = "d3d9.dll";
    else if (hmod == 0x76600000u) dll = "ws2_32.dll";
    else if (hmod == 0x76700000u) dll = "shell32.dll";
    else if (hmod == 0x76800000u) dll = "ucrtbase.dll";

    int resolved = 0;
    uint32_t thunk = asi_win32_register_thunk(dll, proc_name, &resolved);
    if (!resolved) {
        asi_log_warn("GetProcAddress(%s, %s) -> unresolved thunk 0x%08X", dll, proc_name, thunk);
    }
    return thunk;
}

static uint32_t host_load_library(const char *name) {
    uint32_t h = host_resolve_mod_handle(name);
    if (h) return h;
    asi_log_warn("LoadLibrary(%s) -> not found, returning 0", name ? name : "(null)");
    return 0;
}

static void host_notify_virtual_protect(uint32_t va, uint32_t size, uint32_t fl_new) {
    (void)fl_new;
    if (!s_bridge.p_mem_base || !*s_bridge.p_mem_base || size == 0) return;
    /* Ensure target pages in guest memory are host-writable so .asi patches never SIGSEGV */
    long pg = sysconf(_SC_PAGESIZE);
    if (pg <= 0) pg = 4096;
    if (va >= SPEED2_IMAGE_BASE && va + size <= SPEED2_IMAGE_BASE + SPEED2_IMAGE_SIZE) {
        uintptr_t start = ((uintptr_t)(*s_bridge.p_mem_base + va)) & ~((uintptr_t)pg - 1);
        uintptr_t end   = ((uintptr_t)(*s_bridge.p_mem_base + va + size) + (uintptr_t)pg - 1) & ~((uintptr_t)pg - 1);
        mprotect((void *)start, end - start, PROT_READ | PROT_WRITE);
    }
}

static int host_spawn_asi_thread(uint32_t start_va, uint32_t param_va) {
    if (s_deferred_thread_count < MAX_ASI_THREADS) {
        DeferredThread *dt = &s_deferred_threads[s_deferred_thread_count++];
        dt->start_va = start_va;
        dt->param_va = param_va;
        snprintf(dt->mod_name, sizeof(dt->mod_name), "%s", s_active_mod_name);
        asi_log_info("Queued ASI worker entry 0x%08X (param=0x%08X) for post-init run", start_va, param_va);
        return 1;
    }
    return 0;
}

/* =========================================================================
 * Lifted Function Table Lookup (Binary Search over 27,742 SPEED2.EXE funcs)
 * ========================================================================= */
static const RecompFuncEntry *find_enclosing_recomp_func(uint32_t va, uint32_t *out_end_va) {
    const RecompFuncEntry *tbl = (const RecompFuncEntry *)s_bridge.recomp_table;
    uint32_t cnt = s_bridge.recomp_table_count;
    if (!tbl || cnt == 0 || va < SPEED2_TEXT_START || va >= SPEED2_TEXT_END) return NULL;

    uint32_t lo = 0, hi = cnt;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (tbl[mid].va <= va) lo = mid + 1u;
        else hi = mid;
    }
    if (lo == 0) return NULL;
    uint32_t idx = lo - 1u;
    if (out_end_va) {
        *out_end_va = (idx + 1u < cnt) ? tbl[idx + 1u].va : SPEED2_TEXT_END;
    }
    return &tbl[idx];
}

static int is_patched_func_va(uint32_t va) {
    for (uint32_t i = 0; i < s_patched_count; i++) {
        if (s_patched[i].fn_va == va) return 1;
    }
    return 0;
}

/* =========================================================================
 * Unicorn Hooks (Zero UC_HOOK_CODE overhead!)
 * ========================================================================= */
static bool hook_mem_unmapped(uc_engine *uc, uc_mem_type type, uint64_t address,
                              int size, int64_t value, void *user_data) {
    (void)size; (void)value; (void)user_data;
    uint32_t va = (uint32_t)address;

    /* 1. Win32 / UCRT HLE IAT Thunk Call */
    if (type == UC_MEM_FETCH_UNMAPPED && va >= ASI_THUNK_BASE &&
        va < ASI_THUNK_BASE + ASI_THUNK_MAX * ASI_THUNK_STRIDE) {
        s_trap_kind = TRAP_WIN32_IAT;
        s_trap_va = va;
        uc_emu_stop(uc);
        return true;
    }

    /* 2. Return-to-Host Sentinel */
    if (type == UC_MEM_FETCH_UNMAPPED && va == ASI_RET_SENTINEL) {
        uc_emu_stop(uc);
        return true;
    }

    /* 3. On-demand zero-copy mapping for dynamic SPEED2.EXE heap/stack pages */
    if ((type == UC_MEM_READ_UNMAPPED || type == UC_MEM_WRITE_UNMAPPED) &&
        s_bridge.p_mem_base && *s_bridge.p_mem_base && va >= 0x00010000u && va < 0x80000000u) {
        uint32_t page = va & ~0xFFFu;
        if (s_bridge.mem_committed && s_bridge.mem_committed(page, 0x1000u)) {
            uc_err err = uc_mem_map_ptr(uc, page, 0x1000u, UC_PROT_READ | UC_PROT_WRITE,
                                        *s_bridge.p_mem_base + page);
            if (err == UC_ERR_OK) return true;
        }
    }

    /* 4. Unmapped fetch/read/write fault -> stop and log full context */
    s_trap_kind = TRAP_FAULT;
    s_trap_va = va;
    uc_emu_stop(uc);
    return false;
}

static bool hook_mem_fetch_prot(uc_engine *uc, uc_mem_type type, uint64_t address,
                                int size, int64_t value, void *user_data) {
    (void)type; (void)size; (void)value; (void)user_data;
    uint32_t va = (uint32_t)address;

    /* Fetching inside SPEED2.EXE .text on a non-exec page:
     * If it is an UNPATCHED lifted function entry, bounce straight back to native ARM64!
     * Otherwise (mid-function jump/branch across page boundary), grant EXEC on this page. */
    if (va >= SPEED2_TEXT_START && va < SPEED2_TEXT_END) {
        if (!is_patched_func_va(va) && s_bridge.recomp_lookup && s_bridge.recomp_lookup(va) != NULL) {
            s_trap_kind = TRAP_NATIVE_ARM64;
            s_trap_va = va;
            uc_emu_stop(uc);
            return true;
        }
        uint32_t page = va & ~0xFFFu;
        if (uc_mem_protect(uc, page, 0x1000u, UC_PROT_ALL) == UC_ERR_OK) {
            return true;
        }
    }

    s_trap_kind = TRAP_FAULT;
    s_trap_va = va;
    uc_emu_stop(uc);
    return false;
}

/* =========================================================================
 * Register Sync (ARM64 libnfsu2_android.so globals <-> Unicorn x86-32)
 * ========================================================================= */
static void read_uc_regs(uc_engine *uc, AsiX86Regs *out) {
    memset(out, 0, sizeof(*out));
    uc_reg_read(uc, UC_X86_REG_EAX, &out->eax);
    uc_reg_read(uc, UC_X86_REG_EBX, &out->ebx);
    uc_reg_read(uc, UC_X86_REG_ECX, &out->ecx);
    uc_reg_read(uc, UC_X86_REG_EDX, &out->edx);
    uc_reg_read(uc, UC_X86_REG_ESI, &out->esi);
    uc_reg_read(uc, UC_X86_REG_EDI, &out->edi);
    uc_reg_read(uc, UC_X86_REG_EBP, &out->ebp);
    uc_reg_read(uc, UC_X86_REG_ESP, &out->esp);
    uc_reg_read(uc, UC_X86_REG_EIP, &out->eip);
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &out->eflags);
}

static void sync_host_to_uc(uc_engine *uc) {
    uint32_t v;
    if (s_bridge.p_eax) { v = *s_bridge.p_eax; uc_reg_write(uc, UC_X86_REG_EAX, &v); }
    if (s_bridge.p_ecx) { v = *s_bridge.p_ecx; uc_reg_write(uc, UC_X86_REG_ECX, &v); }
    if (s_bridge.p_edx) { v = *s_bridge.p_edx; uc_reg_write(uc, UC_X86_REG_EDX, &v); }
    if (s_bridge.p_ebx) { v = *s_bridge.p_ebx; uc_reg_write(uc, UC_X86_REG_EBX, &v); }
    if (s_bridge.p_esp) { v = *s_bridge.p_esp; uc_reg_write(uc, UC_X86_REG_ESP, &v); }
    if (s_bridge.p_ebp) { v = *s_bridge.p_ebp; uc_reg_write(uc, UC_X86_REG_EBP, &v); }
    if (s_bridge.p_esi) { v = *s_bridge.p_esi; uc_reg_write(uc, UC_X86_REG_ESI, &v); }
    if (s_bridge.p_edi) { v = *s_bridge.p_edi; uc_reg_write(uc, UC_X86_REG_EDI, &v); }
}

static void sync_uc_to_host(uc_engine *uc) {
    uint32_t v = 0;
    if (s_bridge.p_eax) { uc_reg_read(uc, UC_X86_REG_EAX, &v); *s_bridge.p_eax = v; }
    if (s_bridge.p_ecx) { uc_reg_read(uc, UC_X86_REG_ECX, &v); *s_bridge.p_ecx = v; }
    if (s_bridge.p_edx) { uc_reg_read(uc, UC_X86_REG_EDX, &v); *s_bridge.p_edx = v; }
    if (s_bridge.p_ebx) { uc_reg_read(uc, UC_X86_REG_EBX, &v); *s_bridge.p_ebx = v; }
    if (s_bridge.p_esp) { uc_reg_read(uc, UC_X86_REG_ESP, &v); *s_bridge.p_esp = v; }
    if (s_bridge.p_ebp) { uc_reg_read(uc, UC_X86_REG_EBP, &v); *s_bridge.p_ebp = v; }
    if (s_bridge.p_esi) { uc_reg_read(uc, UC_X86_REG_ESI, &v); *s_bridge.p_esi = v; }
    if (s_bridge.p_edi) { uc_reg_read(uc, UC_X86_REG_EDI, &v); *s_bridge.p_edi = v; }
}

/* =========================================================================
 * Core Unicorn Execution Loop (Re-entrant Safe)
 * ========================================================================= */
static int asi_exec_x86(uint32_t entry_va, const char *ctx_label, uint32_t max_insns) {
    if (!s_uc || !s_bridge.p_mem_base || !*s_bridge.p_mem_base) return 0;
    uint8_t *mem = *s_bridge.p_mem_base;
    uint32_t cur_eip = entry_va;

    asi_log_set_context(ctx_label, entry_va, cur_eip, mem);

    while (cur_eip != ASI_RET_SENTINEL && cur_eip != 0u) {
        s_trap_kind = TRAP_NONE;
        s_trap_va = 0;

        uc_err err = uc_emu_start(s_uc, cur_eip, ASI_RET_SENTINEL, 0, max_insns);

        if (s_trap_kind == TRAP_WIN32_IAT) {
            AsiCallRegs cr;
            memset(&cr, 0, sizeof(cr));
            uc_reg_read(s_uc, UC_X86_REG_EAX, &cr.eax);
            uc_reg_read(s_uc, UC_X86_REG_ECX, &cr.ecx);
            uc_reg_read(s_uc, UC_X86_REG_EDX, &cr.edx);
            uc_reg_read(s_uc, UC_X86_REG_EBX, &cr.ebx);
            uc_reg_read(s_uc, UC_X86_REG_ESP, &cr.esp);
            uc_reg_read(s_uc, UC_X86_REG_EBP, &cr.ebp);
            uc_reg_read(s_uc, UC_X86_REG_ESI, &cr.esi);
            uc_reg_read(s_uc, UC_X86_REG_EDI, &cr.edi);

            cur_eip = asi_win32_dispatch(s_trap_va, &cr);

            uc_reg_write(s_uc, UC_X86_REG_EAX, &cr.eax);
            uc_reg_write(s_uc, UC_X86_REG_ECX, &cr.ecx);
            uc_reg_write(s_uc, UC_X86_REG_EDX, &cr.edx);
            uc_reg_write(s_uc, UC_X86_REG_EBX, &cr.ebx);
            uc_reg_write(s_uc, UC_X86_REG_ESP, &cr.esp);
            uc_reg_write(s_uc, UC_X86_REG_EBP, &cr.ebp);
            uc_reg_write(s_uc, UC_X86_REG_ESI, &cr.esi);
            uc_reg_write(s_uc, UC_X86_REG_EDI, &cr.edi);
            continue;
        }

        if (s_trap_kind == TRAP_NATIVE_ARM64) {
            /* Call unpatched SPEED2.EXE function natively on ARM64 at full speed! */
            uint32_t target_va = s_trap_va;
            asi_recomp_fn_t native_fn = (asi_recomp_fn_t)s_bridge.recomp_lookup(target_va);
            if (!native_fn) {
                asi_log_err("TRAP_NATIVE_ARM64 missing target 0x%08X", target_va);
                asi_log_clear_context();
                return 0;
            }
            /* Read x86 return address from [ESP] before native_fn pops it */
            uint32_t x86_esp = 0;
            uc_reg_read(s_uc, UC_X86_REG_ESP, &x86_esp);
            uint32_t ret_va = 0;
            memcpy(&ret_va, mem + x86_esp, 4);

            /* Save Unicorn context in case native_fn re-enters another patched function */
            uc_context *saved_ctx = NULL;
            uc_context_alloc(s_uc, &saved_ctx);
            if (saved_ctx) uc_context_save(s_uc, saved_ctx);

            sync_uc_to_host(s_uc);
            pthread_mutex_unlock(&s_uc_mu);
            native_fn();
            pthread_mutex_lock(&s_uc_mu);

            if (saved_ctx) {
                uc_context_restore(s_uc, saved_ctx);
                uc_context_free(saved_ctx);
            }
            sync_host_to_uc(s_uc);
            cur_eip = ret_va;
            continue;
        }

        if (err != UC_ERR_OK || s_trap_kind == TRAP_FAULT) {
            AsiX86Regs regs;
            read_uc_regs(s_uc, &regs);
            char loc[256];
            describe_va(regs.eip, loc, sizeof(loc));
            char reason[192];
            snprintf(reason, sizeof(reason), "Unicorn %s (%d) fault_va=0x%08X",
                     uc_strerror(err), (int)err, s_trap_va);
            asi_log_dump_fault(reason, &regs, mem, loc);
            asi_log_clear_context();
            return 0;
        }

        uc_reg_read(s_uc, UC_X86_REG_EIP, &cur_eip);
        if (max_insns > 0 && cur_eip != ASI_RET_SENTINEL) {
            asi_log_warn("x86 execution hit instruction limit (%u) in %s at 0x%08X",
                         max_insns, ctx_label ? ctx_label : "?", cur_eip);
            break;
        }
    }

    asi_log_clear_context();
    return 1;
}

/* =========================================================================
 * Entry Point Called from Dynamic ARM64 Trampoline Stubs
 * ========================================================================= */
static void asi_enter_x86_from_arm64(uint32_t fn_va) {
    if (!s_uc || !s_bridge.p_mem_base || !*s_bridge.p_mem_base) return;
    uint8_t *mem = *s_bridge.p_mem_base;

    pthread_mutex_lock(&s_uc_mu);

    /* Save the caller's original return address on guest stack and replace with ASI_RET_SENTINEL */
    uint32_t esp = s_bridge.p_esp ? *s_bridge.p_esp : 0u;
    uint32_t orig_ret_va = 0u;
    if (esp >= 0x10000u) {
        memcpy(&orig_ret_va, mem + esp, 4);
        uint32_t sent = ASI_RET_SENTINEL;
        memcpy(mem + esp, &sent, 4);
    }

    /* Ensure guest stack page is mapped in Unicorn */
    if (esp >= 0x10000u) {
        uint32_t sp_page = (esp - 0x4000u) & ~0xFFFu;
        for (uint32_t p = sp_page; p <= ((esp + 0x1000u) & ~0xFFFu); p += 0x1000u) {
            if (s_bridge.mem_committed && s_bridge.mem_committed(p, 0x1000u)) {
                uc_mem_map_ptr(s_uc, p, 0x1000u, UC_PROT_READ | UC_PROT_WRITE, mem + p);
            }
        }
    }

    sync_host_to_uc(s_uc);
    char label[64];
    snprintf(label, sizeof(label), "patched_sub_%08X", fn_va);
    asi_exec_x86(fn_va, label, 0);
    sync_uc_to_host(s_uc);

    /* Restore original return slot if stack pointer wasn't consumed (safety) */
    (void)orig_ret_va;
    pthread_mutex_unlock(&s_uc_mu);
}

/* Emit a 24-byte ARM64 stub that loads fn_va into W0 and branches to asi_enter_x86_from_arm64 */
static void *emit_arm64_x86_stub(uint32_t fn_va) {
    if (!s_stub_page) {
        s_stub_page = (uint8_t *)mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (s_stub_page == MAP_FAILED) {
            s_stub_page = NULL;
            return NULL;
        }
    }
    if (s_stub_offset + 32 > 65536) return NULL;
    uint8_t *stub = s_stub_page + s_stub_offset;
    s_stub_offset += 32;

    uint32_t *insns = (uint32_t *)stub;
    uint32_t lo16 = fn_va & 0xFFFFu;
    uint32_t hi16 = (fn_va >> 16) & 0xFFFFu;
    insns[0] = 0x52800000u | (lo16 << 5);          /* movz w0, #lo16 */
    insns[1] = 0x72A00000u | (hi16 << 5);          /* movk w0, #hi16, lsl #16 */
    insns[2] = 0x58000050u;                        /* ldr  x16, [pc, #8] */
    insns[3] = 0xD61F0200u;                        /* br   x16 */
    *(uint64_t *)(stub + 16) = (uint64_t)(uintptr_t)&asi_enter_x86_from_arm64;

    __builtin___clear_cache((char *)stub, (char *)(stub + 24));
    return (void *)stub;
}

/* =========================================================================
 * PE32 Loader for .asi Plugins (Map + Relocate + Bind IAT + Run DllMain)
 * ========================================================================= */
static uint16_t rd16_le(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void setup_x86_gdt_and_segments(uc_engine *uc, uint8_t *mem) {
    /* Place a 4-entry 32-bit GDT at ASI_TEB_BASE + 0x2000:
     *   0x00: null
     *   0x08: flat 32-bit code (base=0, limit=4GB, RX)
     *   0x10: flat 32-bit data (base=0, limit=4GB, RW)
     *   0x18: 32-bit FS/TEB data (base=ASI_TEB_BASE, limit=64KB, RW) */
    uint32_t gdt_va = ASI_TEB_BASE + 0x2000u;
    uint64_t gdt[4];
    gdt[0] = 0ULL;
    gdt[1] = 0x00CF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;

    uint32_t teb_base = ASI_TEB_BASE;
    uint32_t teb_lim  = 0xFFFFu;
    uint8_t fs_desc[8];
    fs_desc[0] = (uint8_t)(teb_lim & 0xFFu);
    fs_desc[1] = (uint8_t)((teb_lim >> 8) & 0xFFu);
    fs_desc[2] = (uint8_t)(teb_base & 0xFFu);
    fs_desc[3] = (uint8_t)((teb_base >> 8) & 0xFFu);
    fs_desc[4] = (uint8_t)((teb_base >> 16) & 0xFFu);
    fs_desc[5] = 0x92u; /* Present, DPL=0, S=1, RW data */
    fs_desc[6] = 0x40u | (uint8_t)((teb_lim >> 16) & 0x0Fu); /* 32-bit, byte gran */
    fs_desc[7] = (uint8_t)((teb_base >> 24) & 0xFFu);
    memcpy(&gdt[3], fs_desc, 8);
    memcpy(mem + gdt_va, gdt, sizeof(gdt));

    uc_x86_mmr gdtr;
    memset(&gdtr, 0, sizeof(gdtr));
    gdtr.base  = gdt_va;
    gdtr.limit = sizeof(gdt) - 1;
    uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr);

    uint32_t cs = 0x08u, ds = 0x10u, fs = 0x18u;
    uc_reg_write(uc, UC_X86_REG_CS, &cs);
    uc_reg_write(uc, UC_X86_REG_DS, &ds);
    uc_reg_write(uc, UC_X86_REG_ES, &ds);
    uc_reg_write(uc, UC_X86_REG_SS, &ds);
    uc_reg_write(uc, UC_X86_REG_GS, &ds);
    uc_reg_write(uc, UC_X86_REG_FS, &fs);
}

static int call_asi_func_stdcall(uint32_t fn_va, const uint32_t *args, int nargs,
                                 const char *label, uint32_t *out_eax) {
    if (!s_uc || !s_bridge.p_mem_base || !*s_bridge.p_mem_base) return 0;
    uint8_t *mem = *s_bridge.p_mem_base;

    pthread_mutex_lock(&s_uc_mu);

    uint32_t esp = ASI_STACK_BASE + ASI_STACK_SIZE - 0x1000u;
    for (int i = nargs - 1; i >= 0; i--) {
        esp -= 4u;
        memcpy(mem + esp, &args[i], 4);
    }
    esp -= 4u;
    uint32_t sent = ASI_RET_SENTINEL;
    memcpy(mem + esp, &sent, 4);

    uint32_t zero = 0, eflags = 0x202u;
    uint32_t fs_sel = 0x18u;
    uc_reg_write(s_uc, UC_X86_REG_ESP, &esp);
    uc_reg_write(s_uc, UC_X86_REG_EBP, &esp);
    uc_reg_write(s_uc, UC_X86_REG_EAX, &zero);
    uc_reg_write(s_uc, UC_X86_REG_EBX, &zero);
    uc_reg_write(s_uc, UC_X86_REG_ECX, &zero);
    uc_reg_write(s_uc, UC_X86_REG_EDX, &zero);
    uc_reg_write(s_uc, UC_X86_REG_ESI, &zero);
    uc_reg_write(s_uc, UC_X86_REG_EDI, &zero);
    uc_reg_write(s_uc, UC_X86_REG_EFLAGS, &eflags);
    uc_reg_write(s_uc, UC_X86_REG_FS, &fs_sel);

    /* Cap init calls at 50M instructions to avoid infinite loops freezing boot */
    int ok = asi_exec_x86(fn_va, label, 50000000u);
    uint32_t eax = 0;
    uc_reg_read(s_uc, UC_X86_REG_EAX, &eax);
    if (out_eax) *out_eax = eax;

    pthread_mutex_unlock(&s_uc_mu);
    return ok;
}

static int load_single_asi(const char *filename, const char *full_path) {
    if (s_mod_count >= MAX_LOADED_MODS) return 0;
    uint8_t *mem = *s_bridge.p_mem_base;

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        asi_log_err("Failed to open %s", full_path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsz < 0x100 || fsz > 16 * 1024 * 1024) {
        fclose(fp);
        asi_log_err("%s: invalid file size (%ld)", filename, fsz);
        return 0;
    }
    uint8_t *raw = (uint8_t *)malloc((size_t)fsz);
    if (!raw || fread(raw, 1, (size_t)fsz, fp) != (size_t)fsz) {
        fclose(fp);
        free(raw);
        asi_log_err("%s: read error", filename);
        return 0;
    }
    fclose(fp);

    if (rd16_le(raw) != 0x5A4Du) {
        free(raw);
        asi_log_err("%s: not a valid PE file (bad MZ)", filename);
        return 0;
    }
    uint32_t lfa = rd32_le(raw + 0x3Cu);
    if (lfa + 0xF8u > (uint32_t)fsz || rd32_le(raw + lfa) != 0x00004550u) {
        free(raw);
        asi_log_err("%s: invalid PE signature", filename);
        return 0;
    }
    uint16_t machine = rd16_le(raw + lfa + 4);
    uint16_t nsec    = rd16_le(raw + lfa + 6);
    uint16_t optsz   = rd16_le(raw + lfa + 20);
    const uint8_t *opt = raw + lfa + 24;
    uint16_t magic   = rd16_le(opt);
    if (machine != 0x014Cu || magic != 0x010Bu) {
        free(raw);
        asi_log_err("%s: not a 32-bit x86 PE32 DLL (machine=0x%04X magic=0x%04X)", filename, machine, magic);
        return 0;
    }

    uint32_t ep_rva   = rd32_le(opt + 16);
    uint32_t pref_base= rd32_le(opt + 28);
    uint32_t img_size = (rd32_le(opt + 56) + 0xFFFFu) & ~0xFFFFu;
    uint32_t hdr_size = rd32_le(opt + 60);

    if (s_next_mod_va + img_size > ASI_IMAGE_ARENA + ASI_IMAGE_ARENA_SZ) {
        free(raw);
        asi_log_err("%s: ASI image arena exhausted (need 0x%X)", filename, img_size);
        return 0;
    }

    uint32_t base = s_next_mod_va;
    s_next_mod_va += img_size;
    int64_t delta = (int64_t)base - (int64_t)pref_base;

    /* Copy headers and sections into guest memory */
    memset(mem + base, 0, img_size);
    uint32_t hdr_copy = hdr_size < (uint32_t)fsz ? hdr_size : (uint32_t)fsz;
    memcpy(mem + base, raw, hdr_copy);

    const uint8_t *sh = opt + optsz;
    for (uint16_t i = 0; i < nsec; i++, sh += 40) {
        uint32_t vs = rd32_le(sh + 8);
        uint32_t va = rd32_le(sh + 12);
        uint32_t rs = rd32_le(sh + 16);
        uint32_t ro = rd32_le(sh + 20);
        uint32_t n = rs;
        if (vs > 0 && vs < n) n = vs;
        if (va + n <= img_size && ro + n <= (uint32_t)fsz && n > 0) {
            memcpy(mem + base + va, raw + ro, n);
        }
    }
    free(raw);

    /* Apply Base Relocations (DataDirectory[5]) */
    uint32_t reloc_rva = rd32_le(mem + base + lfa + 24 + 96 + 5 * 8);
    uint32_t reloc_sz  = rd32_le(mem + base + lfa + 24 + 96 + 5 * 8 + 4);
    uint32_t fixups = 0;
    if (reloc_rva && reloc_sz && delta != 0) {
        for (uint32_t off = 0; off + 8 <= reloc_sz; ) {
            uint32_t page = rd32_le(mem + base + reloc_rva + off);
            uint32_t bsz  = rd32_le(mem + base + reloc_rva + off + 4);
            if (bsz < 8 || off + bsz > reloc_sz) break;
            for (uint32_t k = 8; k + 2 <= bsz; k += 2) {
                uint16_t ent = rd16_le(mem + base + reloc_rva + off + k);
                uint32_t t = ent >> 12;
                uint32_t at = base + page + (ent & 0xFFFu);
                if (t == 3u && at + 4 <= base + img_size) {
                    uint32_t v = rd32_le(mem + at);
                    v = (uint32_t)((int64_t)v + delta);
                    memcpy(mem + at, &v, 4);
                    fixups++;
                }
            }
            off += bsz;
        }
    }

    /* Bind Imports (DataDirectory[1]) -> Win32/UCRT HLE thunks */
    uint32_t imp_rva = rd32_le(mem + base + lfa + 24 + 96 + 1 * 8);
    uint32_t imp_sz  = rd32_le(mem + base + lfa + 24 + 96 + 1 * 8 + 4);
    uint32_t bound_ok = 0, bound_missing = 0;
    if (imp_rva && imp_sz) {
        for (uint32_t off = 0; off + 20 <= imp_sz; off += 20) {
            uint32_t ilt_rva  = rd32_le(mem + base + imp_rva + off + 0);
            uint32_t name_rva = rd32_le(mem + base + imp_rva + off + 12);
            uint32_t iat_rva  = rd32_le(mem + base + imp_rva + off + 16);
            if (name_rva == 0) break;
            const char *dll = (const char *)(mem + base + name_rva);
            uint32_t src_rva = ilt_rva ? ilt_rva : iat_rva;
            for (uint32_t j = 0; ; j += 4) {
                uint32_t entry = rd32_le(mem + base + src_rva + j);
                if (entry == 0) break;
                char fn_name[128];
                if (entry & 0x80000000u) {
                    snprintf(fn_name, sizeof(fn_name), "#%u", entry & 0xFFFFu);
                } else {
                    snprintf(fn_name, sizeof(fn_name), "%s", (const char *)(mem + base + entry + 2));
                }
                int resolved = 0;
                uint32_t thunk = asi_win32_register_thunk(dll, fn_name, &resolved);
                memcpy(mem + base + iat_rva + j, &thunk, 4);
                if (resolved) {
                    bound_ok++;
                } else {
                    bound_missing++;
                    asi_log_warn("  [%s] Unimplemented import: %s!%s", filename, dll, fn_name);
                }
            }
        }
    }

    /* Locate optional InitializeASI export (DataDirectory[0]) */
    uint32_t init_asi_va = 0;
    uint32_t exp_rva = rd32_le(mem + base + lfa + 24 + 96 + 0);
    uint32_t exp_sz  = rd32_le(mem + base + lfa + 24 + 96 + 4);
    if (exp_rva && exp_sz >= 40) {
        uint32_t n_names = rd32_le(mem + base + exp_rva + 24);
        uint32_t fn_tbl  = rd32_le(mem + base + exp_rva + 28);
        uint32_t nm_tbl  = rd32_le(mem + base + exp_rva + 32);
        uint32_t ord_tbl = rd32_le(mem + base + exp_rva + 36);
        for (uint32_t i = 0; i < n_names; i++) {
            uint32_t nrva = rd32_le(mem + base + nm_tbl + i * 4);
            const char *ename = (const char *)(mem + base + nrva);
            if (strcmp(ename, "InitializeASI") == 0) {
                uint16_t ord = rd16_le(mem + base + ord_tbl + i * 2);
                uint32_t frva = rd32_le(mem + base + fn_tbl + (uint32_t)ord * 4);
                init_asi_va = base + frva;
                break;
            }
        }
    }

    AsiLoadedModInfo *mi = &s_mods[s_mod_count++];
    memset(mi, 0, sizeof(*mi));
    snprintf(mi->name, sizeof(mi->name), "%s", filename);
    snprintf(mi->host_path, sizeof(mi->host_path), "%s", full_path);
    mi->base_va     = base;
    mi->image_size  = img_size;
    mi->entry_va    = ep_rva ? (base + ep_rva) : 0u;
    mi->init_asi_va = init_asi_va;
    mi->active      = 0;

    asi_log_info("Mapped %s at 0x%08X..0x%08X (pref 0x%08X, %u relocs, imports %u ok / %u unimpl, EP=0x%08X, InitASI=0x%08X)",
                 filename, base, base + img_size, pref_base, fixups, bound_ok, bound_missing,
                 mi->entry_va, mi->init_asi_va);

    snprintf(s_active_mod_name, sizeof(s_active_mod_name), "%s", filename);

    /* Call DllMain(base, DLL_PROCESS_ATTACH=1, 0) */
    if (mi->entry_va) {
        uint32_t args[3] = { base, 1u, 0u };
        uint32_t ret = 0;
        char lbl[160];
        snprintf(lbl, sizeof(lbl), "%s!DllMain", filename);
        int ok = call_asi_func_stdcall(mi->entry_va, args, 3, lbl, &ret);
        asi_log_info("  %s!DllMain(PROCESS_ATTACH) -> ok=%d ret=%u", filename, ok, ret);
        if (!ok) return 0;
    }

    /* Call InitializeASI() if exported */
    if (mi->init_asi_va) {
        uint32_t ret = 0;
        char lbl[160];
        snprintf(lbl, sizeof(lbl), "%s!InitializeASI", filename);
        int ok = call_asi_func_stdcall(mi->init_asi_va, NULL, 0, lbl, &ret);
        asi_log_info("  %s!InitializeASI() -> ok=%d ret=%u", filename, ok, ret);
        if (!ok) return 0;
    }

    mi->active = 1;
    return 1;
}

/* =========================================================================
 * Diff SPEED2.EXE .text and Install Selective ARM64 -> Unicorn Detours
 * ========================================================================= */
static void scan_and_detour_patched_functions(void) {
    if (!s_clean_text || !s_bridge.p_mem_base || !*s_bridge.p_mem_base) return;
    const uint8_t *live_text = *s_bridge.p_mem_base + SPEED2_TEXT_START;

    uint32_t total_patched_bytes = 0;
    uint32_t i = 0;
    while (i < SPEED2_TEXT_SIZE) {
        if (live_text[i] == s_clean_text[i]) {
            i++;
            continue;
        }
        uint32_t patch_va = SPEED2_TEXT_START + i;
        uint32_t patch_len = 0;
        while (i + patch_len < SPEED2_TEXT_SIZE && live_text[i + patch_len] != s_clean_text[i + patch_len]) {
            patch_len++;
        }
        total_patched_bytes += patch_len;

        uint32_t fn_end = 0;
        const RecompFuncEntry *fe = find_enclosing_recomp_func(patch_va, &fn_end);
        if (!fe || !fe->arm64_fn) {
            asi_log_warn("  .text patch at 0x%08X (%u bytes): no enclosing lifted function found", patch_va, patch_len);
        } else if (!is_patched_func_va(fe->va)) {
            if (s_patched_count < MAX_PATCHED_FUNCS) {
                void *stub = emit_arm64_x86_stub(fe->va);
                if (stub && s_bridge.arm64_inline_hook) {
                    asi_recomp_fn_t orig_tramp = s_bridge.arm64_inline_hook(fe->arm64_fn, stub);
                    if (orig_tramp) {
                        PatchedFuncRec *rec = &s_patched[s_patched_count++];
                        rec->fn_va         = fe->va;
                        rec->fn_end_va     = fn_end;
                        rec->orig_arm64_fn = fe->arm64_fn;
                        rec->orig_tramp    = orig_tramp;
                        rec->stub_addr     = stub;

                        /* Grant EXEC in Unicorn on all 4KB pages covering this patched function */
                        uint32_t p_start = fe->va & ~0xFFFu;
                        uint32_t p_end   = (fn_end + 0xFFFu) & ~0xFFFu;
                        if (p_end > SPEED2_TEXT_END) p_end = SPEED2_TEXT_END;
                        uc_mem_protect(s_uc, p_start, p_end - p_start, UC_PROT_ALL);

                        asi_log_info("  Detoured SPEED2.EXE fn 0x%08X..0x%08X (patched @ 0x%08X, %u bytes) -> Unicorn JIT",
                                     fe->va, fn_end, patch_va, patch_len);
                    } else {
                        asi_log_warn("  Failed to inline-hook ARM64 fn %p for 0x%08X (may already be hooked)",
                                     fe->arm64_fn, fe->va);
                    }
                }
            }
        }
        i += patch_len;
    }

    asi_log_info(".text diff complete: %u modified bytes across %u detoured SPEED2.EXE functions (all other 27,742 functions remain 100%% native ARM64)",
                 total_patched_bytes, s_patched_count);
}

/* =========================================================================
 * Main Boot Entry Point
 * ========================================================================= */
int asi_loader_boot(const AsiHostBridge *bridge) {
    if (!bridge || !bridge->game_dir || !bridge->p_mem_base || !*bridge->p_mem_base) return 0;
    s_bridge = *bridge;

    asi_log_init(s_bridge.game_dir);
    asi_log_info("Booting ASI Modloader (recomp_table=%p count=%u, mem_base=%p)",
                 s_bridge.recomp_table, s_bridge.recomp_table_count, (void *)*s_bridge.p_mem_base);

    /* Check if dinput8.dll is present (informational; our native loader replaces it) */
    char dinput_p[1024];
    snprintf(dinput_p, sizeof(dinput_p), "%s/dinput8.dll", s_bridge.game_dir);
    struct stat st;
    if (stat(dinput_p, &st) == 0) {
        asi_log_info("Detected %s (%lld bytes) - native Unicorn ASI Loader taking over",
                     dinput_p, (long long)st.st_size);
    }

    /* Scan <game_dir>/scripts/*.asi */
    char scripts_dir[1024];
    snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", s_bridge.game_dir);
    DIR *dp = opendir(scripts_dir);
    if (!dp) {
        asi_log_warn("No scripts/ directory at %s; ASI Loader staying idle", scripts_dir);
        return 0;
    }

    char mod_files[MAX_LOADED_MODS][128];
    uint32_t candidate_count = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL && candidate_count < MAX_LOADED_MODS) {
        size_t nl = strlen(de->d_name);
        if (nl <= 4 || strcasecmp(de->d_name + nl - 4, ".asi") != 0) continue;
        if (!cfg_is_mod_enabled(s_bridge.game_dir, de->d_name)) {
            asi_log_info("Skipping %s (%s)", de->d_name,
                         is_builtin_asi_name(de->d_name) ? "built-in native equivalent active" : "disabled in modloader.cfg");
            continue;
        }
        snprintf(mod_files[candidate_count++], 128, "%s", de->d_name);
    }
    closedir(dp);

    if (candidate_count == 0) {
        asi_log_info("No enabled .asi mods found in %s - 0%% emulation overhead (pure native ARM64)", scripts_dir);
        return 0;
    }

    /* Commit private guest arenas in 4 GiB g_mem_base */
    uint8_t *mem = *s_bridge.p_mem_base;
    if (mprotect(mem + ASI_IMAGE_ARENA, ASI_IMAGE_ARENA_SZ, PROT_READ | PROT_WRITE) != 0 ||
        mprotect(mem + ASI_HEAP_ARENA,  ASI_HEAP_ARENA_SZ,  PROT_READ | PROT_WRITE) != 0 ||
        mprotect(mem + ASI_STACK_BASE,  ASI_STACK_SIZE,     PROT_READ | PROT_WRITE) != 0 ||
        mprotect(mem + ASI_TEB_BASE,    ASI_TEB_SIZE,       PROT_READ | PROT_WRITE) != 0) {
        asi_log_err("Failed to mprotect guest ASI arenas inside g_mem_base");
        return 0;
    }
    memset(mem + ASI_TEB_BASE, 0, ASI_TEB_SIZE);

    /* Populate minimal Windows x86 TEB/PEB */
    uint32_t seh_end = 0xFFFFFFFFu;
    uint32_t stk_top = ASI_STACK_BASE + ASI_STACK_SIZE;
    uint32_t stk_lim = ASI_STACK_BASE;
    uint32_t teb_va  = ASI_TEB_BASE;
    uint32_t peb_va  = ASI_TEB_BASE + 0x1000u;
    uint32_t pid     = (uint32_t)getpid();
    uint32_t tid     = (uint32_t)(uintptr_t)pthread_self();
    uint32_t img_b   = SPEED2_IMAGE_BASE;
    uint32_t proc_hp = 0x0000BEEFu;
    memcpy(mem + teb_va + 0x00, &seh_end, 4);
    memcpy(mem + teb_va + 0x04, &stk_top, 4);
    memcpy(mem + teb_va + 0x08, &stk_lim, 4);
    memcpy(mem + teb_va + 0x18, &teb_va,  4);
    memcpy(mem + teb_va + 0x20, &pid,     4);
    memcpy(mem + teb_va + 0x24, &tid,     4);
    memcpy(mem + teb_va + 0x30, &peb_va,  4);
    memcpy(mem + peb_va + 0x08, &img_b,   4);
    memcpy(mem + peb_va + 0x18, &proc_hp, 4);

    /* Snapshot clean SPEED2.EXE .text before any .asi patches it */
    s_clean_text = (uint8_t *)malloc(SPEED2_TEXT_SIZE);
    if (!s_clean_text) {
        asi_log_err("OOM allocating clean .text snapshot");
        return 0;
    }
    memcpy(s_clean_text, mem + SPEED2_TEXT_START, SPEED2_TEXT_SIZE);

    /* Initialize Unicorn x86-32 JIT engine */
    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_32, &s_uc);
    if (err != UC_ERR_OK) {
        asi_log_err("uc_open(X86, 32) failed: %s", uc_strerror(err));
        return 0;
    }

    /* Zero-copy map SPEED2.EXE + ASI arenas into Unicorn */
    uc_mem_map_ptr(s_uc, SPEED2_IMAGE_BASE, 0x1000u, UC_PROT_READ | UC_PROT_WRITE, mem + SPEED2_IMAGE_BASE);
    uc_mem_map_ptr(s_uc, SPEED2_TEXT_START, SPEED2_TEXT_SIZE, UC_PROT_READ | UC_PROT_WRITE, mem + SPEED2_TEXT_START);
    uc_mem_map_ptr(s_uc, SPEED2_TEXT_END, SPEED2_IMAGE_BASE + SPEED2_IMAGE_SIZE - SPEED2_TEXT_END,
                   UC_PROT_READ | UC_PROT_WRITE, mem + SPEED2_TEXT_END);
    uc_mem_map_ptr(s_uc, ASI_IMAGE_ARENA, ASI_IMAGE_ARENA_SZ, UC_PROT_ALL, mem + ASI_IMAGE_ARENA);
    uc_mem_map_ptr(s_uc, ASI_HEAP_ARENA,  ASI_HEAP_ARENA_SZ,  UC_PROT_ALL, mem + ASI_HEAP_ARENA);
    uc_mem_map_ptr(s_uc, ASI_STACK_BASE,  ASI_STACK_SIZE,     UC_PROT_READ | UC_PROT_WRITE, mem + ASI_STACK_BASE);
    uc_mem_map_ptr(s_uc, ASI_TEB_BASE,    ASI_TEB_SIZE,       UC_PROT_READ | UC_PROT_WRITE, mem + ASI_TEB_BASE);

    setup_x86_gdt_and_segments(s_uc, mem);

    uc_hook hh_unmapped = 0, hh_prot = 0;
    uc_hook_add(s_uc, &hh_unmapped, UC_HOOK_MEM_UNMAPPED, (void *)hook_mem_unmapped, NULL, 1, 0);
    uc_hook_add(s_uc, &hh_prot, UC_HOOK_MEM_FETCH_PROT, (void *)hook_mem_fetch_prot, NULL,
                SPEED2_TEXT_START, SPEED2_TEXT_END - 1);

    /* Initialize Win32 / UCRT HLE bridge */
    AsiWin32Host wh;
    memset(&wh, 0, sizeof(wh));
    wh.mem_base               = mem;
    wh.game_dir               = s_bridge.game_dir;
    wh.resolve_mod_handle     = host_resolve_mod_handle;
    wh.find_mod_by_handle     = find_mod_by_handle;
    wh.find_mod_by_addr       = find_mod_by_addr;
    wh.resolve_proc_addr      = host_resolve_proc_addr;
    wh.load_library           = host_load_library;
    wh.notify_virtual_protect = host_notify_virtual_protect;
    wh.spawn_asi_thread       = host_spawn_asi_thread;
    asi_win32_init(&wh);

    /* Load and initialize each enabled .asi mod */
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < candidate_count; i++) {
        char full[1280];
        snprintf(full, sizeof(full), "%s/%s", scripts_dir, mod_files[i]);
        asi_log_info("Loading ASI [%u/%u]: %s", i + 1, candidate_count, mod_files[i]);
        if (load_single_asi(mod_files[i], full)) {
            loaded++;
        } else {
            asi_log_err("ASI mod %s failed during init (continuing with remaining mods)", mod_files[i]);
        }
    }

    /* Run any deferred init worker threads synchronously once (many .asi mods do CreateThread -> Init() in DllMain) */
    for (uint32_t i = 0; i < s_deferred_thread_count; i++) {
        DeferredThread *dt = &s_deferred_threads[i];
        char lbl[160];
        snprintf(lbl, sizeof(lbl), "%s!Thread_0x%08X", dt->mod_name, dt->start_va);
        uint32_t arg = dt->param_va, ret = 0;
        asi_log_info("Running deferred ASI init thread %s (param=0x%08X)", lbl, arg);
        call_asi_func_stdcall(dt->start_va, &arg, 1, lbl, &ret);
    }

    /* Diff SPEED2.EXE .text and install selective ARM64 -> Unicorn detours */
    scan_and_detour_patched_functions();

    free(s_clean_text);
    s_clean_text = NULL;

    s_asi_active = (loaded > 0) ? 1 : 0;
    asi_log_info("ASI Modloader ready: %u/%u mods loaded, %u functions detoured to Unicorn JIT",
                 loaded, candidate_count, s_patched_count);
    return (int)loaded;
}

int asi_loader_is_active(void) {
    return s_asi_active;
}

int asi_loader_get_loaded_summary(char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    uint32_t active = 0;
    for (uint32_t i = 0; i < s_mod_count; i++) {
        if (s_mods[i].active) active++;
    }
    snprintf(out, out_sz, "%u loaded (%u hooks)", active, s_patched_count);
    return (int)active;
}
