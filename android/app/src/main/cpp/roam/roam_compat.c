/* Runtime compatibility shim for the prebuilt libnfsu2_android.so.
 *
 * CI packages a prebuilt libnfsu2_android.so (last updated 2026-11-03) and
 * does not rebuild it. Fixes that live only in guest_runtime.cpp /
 * guest_kernel.inc therefore never reach the APK. This file is compiled into
 * libnfsu2_roam.so (which CI does rebuild) and installs hooks:
 *
 *   G1  recomp_unresolved  — return EAX=0 for NULL ICALL at 0x005c5c00
 *                            instead of throwing GuestStop / halting boot.
 *   G3  AAudio             — register an error callback the prebuilt lacks,
 *                            and recreate the stream on DISCONNECT / JNI
 *                            nativeRequestAudioRestart (incoming call).
 *   G4  Speedometer JNI    — provide the missing
 *                            nativeSpeedometerPosition symbol; re-apply
 *                            minimap offset each frame from NFSU2_MINIMAP.
 *   G7  Multi-user storage — rewrite /storage/emulated/0/... opens from
 *                            the game library to the current Android user.
 *
 * G5 (garage hook recursion) is fixed in roam_android.c itself.
 *   G2  Guest CS deadlock  — GOT-wrap pthread_mutex_trylock/unlock in the
 *                            prebuilt runtime and break ABBA waits on two
 *                            recursive_mutexes after 400 failed trylocks,
 *                            pairing a fake acquire with a matching unlock
 *                            no-op (same idea as guest_kernel.inc 78d45b9).
 *   G6  Enhanced Contrast  — ARM64-hook lifted 0x005C68D0 so the 3-pass
 *                            full-screen LockRect blit is skipped; the
 *                            existing SetGammaRamp S-curve already applies
 *                            the look on change, at zero extra bandwidth.
 */

#define _GNU_SOURCE
#include "roam_compat.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <link.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define TAG "NFSU2-Compat"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)


/* Load libaaudio.so at runtime so we do not need -laaudio on the roam link line
 * (the workflow file cannot be changed: the push token has no `workflow` scope). */
static void *s_libaaudio;

#define AA_LOAD(name) do { *(void **)(&s_##name) = dlsym(s_libaaudio, #name); } while (0)

static aaudio_result_t (*s_AAudio_createStreamBuilder)(AAudioStreamBuilder **);
static aaudio_result_t (*s_AAudioStreamBuilder_delete)(AAudioStreamBuilder *);
static aaudio_result_t (*s_AAudioStreamBuilder_setDirection)(AAudioStreamBuilder *, aaudio_direction_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setFormat)(AAudioStreamBuilder *, aaudio_format_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setChannelCount)(AAudioStreamBuilder *, int32_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setSharingMode)(AAudioStreamBuilder *, aaudio_sharing_mode_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setPerformanceMode)(AAudioStreamBuilder *, aaudio_performance_mode_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setUsage)(AAudioStreamBuilder *, aaudio_usage_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setContentType)(AAudioStreamBuilder *, aaudio_content_type_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setBufferCapacityInFrames)(AAudioStreamBuilder *, int32_t);
static aaudio_result_t (*s_AAudioStreamBuilder_setDataCallback)(AAudioStreamBuilder *, AAudioStream_dataCallback, void *);
static aaudio_result_t (*s_AAudioStreamBuilder_setErrorCallback)(AAudioStreamBuilder *, AAudioStream_errorCallback, void *);
static aaudio_result_t (*s_AAudioStreamBuilder_openStream)(AAudioStreamBuilder *, AAudioStream **);
static aaudio_result_t (*s_AAudioStream_close)(AAudioStream *);
static aaudio_result_t (*s_AAudioStream_requestStart)(AAudioStream *);
static aaudio_result_t (*s_AAudioStream_requestStop)(AAudioStream *);
static aaudio_result_t (*s_AAudioStream_setBufferSizeInFrames)(AAudioStream *, int32_t);
static int32_t (*s_AAudioStream_getBufferSizeInFrames)(AAudioStream *);
static int32_t (*s_AAudioStream_getSampleRate)(AAudioStream *);
static aaudio_stream_state_t (*s_AAudioStream_getState)(AAudioStream *);

static int load_aaudio(void) {
    if (s_libaaudio) return 1;
    s_libaaudio = dlopen("libaaudio.so", RTLD_NOW);
    if (!s_libaaudio) {
        LOGE("dlopen libaaudio.so failed: %s", dlerror());
        return 0;
    }
    AA_LOAD(AAudio_createStreamBuilder);
    AA_LOAD(AAudioStreamBuilder_delete);
    AA_LOAD(AAudioStreamBuilder_setDirection);
    AA_LOAD(AAudioStreamBuilder_setFormat);
    AA_LOAD(AAudioStreamBuilder_setChannelCount);
    AA_LOAD(AAudioStreamBuilder_setSharingMode);
    AA_LOAD(AAudioStreamBuilder_setPerformanceMode);
    AA_LOAD(AAudioStreamBuilder_setUsage);
    AA_LOAD(AAudioStreamBuilder_setContentType);
    AA_LOAD(AAudioStreamBuilder_setBufferCapacityInFrames);
    AA_LOAD(AAudioStreamBuilder_setDataCallback);
    AA_LOAD(AAudioStreamBuilder_setErrorCallback);
    AA_LOAD(AAudioStreamBuilder_openStream);
    AA_LOAD(AAudioStream_close);
    AA_LOAD(AAudioStream_requestStart);
    AA_LOAD(AAudioStream_requestStop);
    AA_LOAD(AAudioStream_setBufferSizeInFrames);
    AA_LOAD(AAudioStream_getBufferSizeInFrames);
    AA_LOAD(AAudioStream_getSampleRate);
    AA_LOAD(AAudioStream_getState);
    if (!s_AAudio_createStreamBuilder || !s_AAudioStreamBuilder_openStream) {
        LOGE("libaaudio.so is missing required symbols");
        return 0;
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* ARM64 inline hook (same trampoline style as roam_android.c)                */
/* -------------------------------------------------------------------------- */

typedef void (*void_fn_t)(void);

static uint8_t *s_tramp_page;
static size_t   s_tramp_off;

static void *arm64_hook(void *target_func, void *hook_func) {
    if (!target_func || !hook_func) return NULL;
    uint32_t *target = (uint32_t *)target_func;

    if (!s_tramp_page) {
        s_tramp_page = (uint8_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (s_tramp_page == MAP_FAILED) {
            s_tramp_page = NULL;
            LOGE("mmap trampoline page failed");
            return NULL;
        }
    }
    if (s_tramp_off + 64 > 4096) {
        LOGE("trampoline page full");
        return NULL;
    }
    uint8_t *tramp = s_tramp_page + s_tramp_off;
    s_tramp_off += 64;

    memcpy(tramp, target, 16);
    uint32_t *t_code = (uint32_t *)(tramp + 16);
    t_code[0] = 0x58000050; /* ldr x16, [pc, #8] */
    t_code[1] = 0xd61f0200; /* br x16 */
    *(uint64_t *)(tramp + 24) = (uint64_t)((uintptr_t)target + 16);
    __builtin___clear_cache((char *)tramp, (char *)tramp + 32);

    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 16384;
    uintptr_t page_start = (uintptr_t)target & ~((uintptr_t)page_sz - 1);
    if (mprotect((void *)page_start, (size_t)page_sz * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("mprotect failed for target %p: %s", target, strerror(errno));
        return NULL;
    }
    target[0] = 0x58000050;
    target[1] = 0xd61f0200;
    *(uint64_t *)(&target[2]) = (uint64_t)((uintptr_t)hook_func);
    __builtin___clear_cache((char *)target, (char *)target + 16);
    return tramp;
}

/* -------------------------------------------------------------------------- */
/* Locate libnfsu2_android.so and its GOT slots                               */
/* -------------------------------------------------------------------------- */

static void *s_game_base;
static uint32_t *s_g_eax;

static void *game_base(void) {
    if (s_game_base) return s_game_base;
    void *sym = dlsym(RTLD_DEFAULT, "recomp_unresolved");
    if (!sym) {
        void *h = dlopen("libnfsu2_android.so", RTLD_NOLOAD);
        if (h) sym = dlsym(h, "recomp_unresolved");
    }
    if (!sym) return NULL;
    Dl_info info;
    if (!dladdr(sym, &info) || !info.dli_fbase) return NULL;
    s_game_base = info.dli_fbase;
    return s_game_base;
}

static uintptr_t dyn_addr(void *base, Elf64_Addr ptr) {
    /* After load, Bionic leaves DT_* pointers as absolute addresses when they
     * already sit above the load base; otherwise they are vaddrs. */
    if (ptr == 0) return 0;
    if (ptr >= (Elf64_Addr)(uintptr_t)base) return (uintptr_t)ptr;
    return (uintptr_t)base + (uintptr_t)ptr;
}

static void **find_got_slot(void *base, const char *name) {
    if (!base || !name) return NULL;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return NULL;
    const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)base + eh->e_phoff);
    const Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn = (const Elf64_Dyn *)((const uint8_t *)base + ph[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return NULL;

    const Elf64_Sym *symtab = NULL;
    const char *strtab = NULL;
    const Elf64_Rela *jmprel = NULL;
    size_t jmprelsz = 0;
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_SYMTAB: symtab = (const Elf64_Sym *)dyn_addr(base, d->d_un.d_ptr); break;
        case DT_STRTAB: strtab = (const char *)dyn_addr(base, d->d_un.d_ptr); break;
        case DT_JMPREL: jmprel = (const Elf64_Rela *)dyn_addr(base, d->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmprelsz = (size_t)d->d_un.d_val; break;
        default: break;
        }
    }
    if (!symtab || !strtab || !jmprel || !jmprelsz) return NULL;

    size_t n = jmprelsz / sizeof(Elf64_Rela);
    for (size_t i = 0; i < n; i++) {
        uint32_t sym_idx = ELF64_R_SYM(jmprel[i].r_info);
        const char *sname = strtab + symtab[sym_idx].st_name;
        if (strcmp(sname, name) == 0) {
            return (void **)dyn_addr(base, jmprel[i].r_offset);
        }
    }
    return NULL;
}

static int patch_got(void *base, const char *name, void *hook, void **orig_out) {
    void **slot = find_got_slot(base, name);
    if (!slot) {
        LOGW("GOT slot for %s not found", name);
        return -1;
    }
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 16384;
    uintptr_t page = (uintptr_t)slot & ~((uintptr_t)page_sz - 1);
    if (mprotect((void *)page, (size_t)page_sz * 2, PROT_READ | PROT_WRITE) != 0) {
        LOGE("mprotect GOT %s failed: %s", name, strerror(errno));
        return -1;
    }
    if (orig_out) *orig_out = *slot;
    *slot = hook;
    LOGI("hooked %s GOT %p: %p -> %p", name, (void *)slot, orig_out ? *orig_out : NULL, hook);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* G7: rewrite /storage/emulated/0 to the current Android multi-user path      */
/* -------------------------------------------------------------------------- */

static int android_user_id(void) {
    return (int)(getuid() / 100000);
}

static const char *rewrite_emulated0(const char *path, char *buf, size_t buflen) {
    int uid = android_user_id();
    if (uid == 0 || !path) return path;
    const char *prefix = "/storage/emulated/0";
    size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0) return path;
    if (path[n] != '\0' && path[n] != '/') return path;
    int wr = snprintf(buf, buflen, "/storage/emulated/%d%s", uid, path + n);
    if (wr < 0 || (size_t)wr >= buflen) return path;
    return buf;
}

static int        (*s_orig_access)(const char *, int);
static FILE      *(*s_orig_fopen)(const char *, const char *);
static int        (*s_orig_stat)(const char *, struct stat *);
static int        (*s_orig_lstat)(const char *, struct stat *);
static int        (*s_orig_open)(const char *, int, ...);
static int        (*s_orig_open_2)(const char *, int);
static int        (*s_orig_openat)(int, const char *, int, ...);
static DIR       *(*s_orig_opendir)(const char *);

static int hook_access(const char *path, int mode) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    return s_orig_access ? s_orig_access(p, mode) : access(p, mode);
}
static FILE *hook_fopen(const char *path, const char *mode) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    return s_orig_fopen ? s_orig_fopen(p, mode) : fopen(p, mode);
}
static int hook_stat(const char *path, struct stat *st) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    return s_orig_stat ? s_orig_stat(p, st) : stat(p, st);
}
static int hook_lstat(const char *path, struct stat *st) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    return s_orig_lstat ? s_orig_lstat(p, st) : lstat(p, st);
}
static int hook_open(const char *path, int flags, ...) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (s_orig_open) return s_orig_open(p, flags, mode);
    return open(p, flags, mode);
}
static int hook_open_2(const char *path, int flags) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    if (s_orig_open_2) return s_orig_open_2(p, flags);
    return open(p, flags);
}
static int hook_openat(int dirfd, const char *path, int flags, ...) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (s_orig_openat) return s_orig_openat(dirfd, p, flags, mode);
    return openat(dirfd, p, flags, mode);
}
static DIR *hook_opendir(const char *path) {
    char buf[1024];
    const char *p = rewrite_emulated0(path, buf, sizeof buf);
    return s_orig_opendir ? s_orig_opendir(p) : opendir(p);
}

static void install_path_hooks(void *base) {
    if (android_user_id() == 0) {
        LOGI("primary Android user; no storage-path rewrite");
        return;
    }
    LOGI("Android user %d: rewriting /storage/emulated/0 for the game library", android_user_id());
    patch_got(base, "access",  (void *)hook_access,  (void **)&s_orig_access);
    patch_got(base, "fopen",   (void *)hook_fopen,   (void **)&s_orig_fopen);
    patch_got(base, "stat",    (void *)hook_stat,    (void **)&s_orig_stat);
    patch_got(base, "lstat",   (void *)hook_lstat,   (void **)&s_orig_lstat);
    patch_got(base, "open",    (void *)hook_open,    (void **)&s_orig_open);
    patch_got(base, "__open_2",(void *)hook_open_2,  (void **)&s_orig_open_2);
    patch_got(base, "openat",  (void *)hook_openat,  (void **)&s_orig_openat);
    patch_got(base, "opendir", (void *)hook_opendir, (void **)&s_orig_opendir);
}

/* -------------------------------------------------------------------------- */
/* G1: graceful recovery from NULL ICALL at 0x005c5c00                        */
/* -------------------------------------------------------------------------- */

typedef void (*unresolved_fn)(const char *kind, uint32_t va, uint32_t from);
static unresolved_fn s_orig_unresolved;

static void hook_recomp_unresolved(const char *kind, uint32_t va, uint32_t from) {
    if (from == 0x005c5c00u || from == 0x00725520u || va == 0u || va == 0x005c5c00u) {
        static uint32_t last_from, log_count;
        if (last_from != from || ++log_count <= 10) {
            last_from = from;
            LOGW("Recovering gracefully from unresolved %s %08x at %08x (boot continues)",
                 kind ? kind : "ICALL", va, from);
        }
        if (!s_g_eax) s_g_eax = (uint32_t *)dlsym(RTLD_DEFAULT, "g_eax");
        if (s_g_eax) *s_g_eax = 0;
        return;
    }
    if (s_orig_unresolved) s_orig_unresolved(kind, va, from);
}

static void install_icall_hook(void) {
    void *sym = dlsym(RTLD_DEFAULT, "recomp_unresolved");
    if (!sym) {
        LOGE("recomp_unresolved not found");
        return;
    }
    s_orig_unresolved = (unresolved_fn)arm64_hook(sym, (void *)hook_recomp_unresolved);
    s_g_eax = (uint32_t *)dlsym(RTLD_DEFAULT, "g_eax");
    if (s_orig_unresolved) {
        LOGI("hooked recomp_unresolved at %p (orig trampoline %p, g_eax %p)",
             sym, (void *)s_orig_unresolved, (void *)s_g_eax);
    } else {
        LOGE("failed to hook recomp_unresolved");
    }
}

/* -------------------------------------------------------------------------- */
/* G3: AAudio disconnect recovery                                             */
/* -------------------------------------------------------------------------- */

static AAudioStream *s_captured_stream;
static AAudioStream_dataCallback s_game_data_cb;
static void *s_game_data_ud;
static AAudioStream_errorCallback s_game_error_cb;
static void *s_game_error_ud;
static int32_t s_fmt = AAUDIO_FORMAT_PCM_FLOAT;
static int32_t s_channels = 2;
static int32_t s_direction = AAUDIO_DIRECTION_OUTPUT;
static int32_t s_sharing = AAUDIO_SHARING_MODE_SHARED;
static int32_t s_perf = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
static int32_t s_capacity;
static int32_t s_sample_rate;
static int32_t s_buffer_size;
static atomic_int s_restart_pending;
static atomic_int s_muted;
static pthread_mutex_t s_audio_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_audio_thread;
static atomic_int s_audio_thread_started;

static aaudio_data_callback_result_t
wrapped_data_cb(AAudioStream *stream, void *ud, void *data, int32_t frames) {
    aaudio_data_callback_result_t r = AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (s_game_data_cb) r = s_game_data_cb(stream, ud ? ud : s_game_data_ud, data, frames);
    if (atomic_load(&s_muted) && data && frames > 0) {
        /* Stereo float is 8 bytes/frame; also covers 16-bit stereo (4 bytes) if we over-clear. */
        size_t bytes = (size_t)frames * 8u;
        memset(data, 0, bytes);
    }
    return r;
}

static void wrapped_error_cb(AAudioStream *stream, void *ud, aaudio_result_t error) {
    LOGW("AAudio error %d on stream %p — requesting reopen", (int)error, (void *)stream);
    if (error == AAUDIO_ERROR_DISCONNECTED || error == AAUDIO_ERROR_TIMEOUT ||
        error == AAUDIO_ERROR_INTERNAL     || error == AAUDIO_ERROR_INVALID_STATE) {
        atomic_store(&s_restart_pending, 1);
    } else if (error < 0) {
        atomic_store(&s_restart_pending, 1);
    }
    if (s_game_error_cb) s_game_error_cb(stream, ud ? ud : s_game_error_ud, error);
}

typedef aaudio_result_t (*set_data_cb_fn)(AAudioStreamBuilder *, AAudioStream_dataCallback, void *);
typedef aaudio_result_t (*set_err_cb_fn)(AAudioStreamBuilder *, AAudioStream_errorCallback, void *);
typedef aaudio_result_t (*open_fn)(AAudioStreamBuilder *, AAudioStream **);
typedef aaudio_result_t (*close_fn)(AAudioStream *);
typedef aaudio_result_t (*set_fmt_fn)(AAudioStreamBuilder *, aaudio_format_t);
typedef aaudio_result_t (*set_ch_fn)(AAudioStreamBuilder *, int32_t);
typedef aaudio_result_t (*set_perf_fn)(AAudioStreamBuilder *, aaudio_performance_mode_t);
typedef aaudio_result_t (*set_share_fn)(AAudioStreamBuilder *, aaudio_sharing_mode_t);
typedef aaudio_result_t (*set_cap_fn)(AAudioStreamBuilder *, int32_t);
typedef aaudio_result_t (*set_dir_fn)(AAudioStreamBuilder *, aaudio_direction_t);

static set_data_cb_fn s_orig_set_data_cb;
static open_fn        s_orig_open_stream;
static close_fn       s_orig_close_stream;
static set_fmt_fn     s_orig_set_fmt;
static set_ch_fn      s_orig_set_ch;
static set_perf_fn    s_orig_set_perf;
static set_share_fn   s_orig_set_share;
static set_cap_fn     s_orig_set_cap;
static set_dir_fn     s_orig_set_dir;

static aaudio_result_t hook_set_data_cb(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *ud) {
    s_game_data_cb = cb;
    s_game_data_ud = ud;
    if (s_orig_set_data_cb) return s_orig_set_data_cb(b, wrapped_data_cb, ud);
    return s_AAudioStreamBuilder_setDataCallback(b, wrapped_data_cb, ud);
}
static aaudio_result_t hook_set_fmt(AAudioStreamBuilder *b, aaudio_format_t f) {
    s_fmt = (int32_t)f;
    return s_orig_set_fmt ? s_orig_set_fmt(b, f) : s_AAudioStreamBuilder_setFormat(b, f);
}
static aaudio_result_t hook_set_ch(AAudioStreamBuilder *b, int32_t n) {
    s_channels = n;
    return s_orig_set_ch ? s_orig_set_ch(b, n) : s_AAudioStreamBuilder_setChannelCount(b, n);
}
static aaudio_result_t hook_set_perf(AAudioStreamBuilder *b, aaudio_performance_mode_t m) {
    s_perf = (int32_t)m;
    return s_orig_set_perf ? s_orig_set_perf(b, m) : s_AAudioStreamBuilder_setPerformanceMode(b, m);
}
static aaudio_result_t hook_set_share(AAudioStreamBuilder *b, aaudio_sharing_mode_t m) {
    s_sharing = (int32_t)m;
    return s_orig_set_share ? s_orig_set_share(b, m) : s_AAudioStreamBuilder_setSharingMode(b, m);
}
static aaudio_result_t hook_set_cap(AAudioStreamBuilder *b, int32_t n) {
    s_capacity = n;
    return s_orig_set_cap ? s_orig_set_cap(b, n) : s_AAudioStreamBuilder_setBufferCapacityInFrames(b, n);
}
static aaudio_result_t hook_set_dir(AAudioStreamBuilder *b, aaudio_direction_t d) {
    s_direction = (int32_t)d;
    return s_orig_set_dir ? s_orig_set_dir(b, d) : s_AAudioStreamBuilder_setDirection(b, d);
}

static aaudio_result_t hook_open_stream(AAudioStreamBuilder *b, AAudioStream **out) {
    /* The Nov 3 prebuilt never registers an error callback. Attach ours so a
     * route change or incoming call can reopen the stream. */
    s_AAudioStreamBuilder_setErrorCallback(b, wrapped_error_cb, NULL);
    if (s_game_data_cb)
        s_AAudioStreamBuilder_setDataCallback(b, wrapped_data_cb, s_game_data_ud);
    aaudio_result_t st = s_orig_open_stream ? s_orig_open_stream(b, out)
                                            : s_AAudioStreamBuilder_openStream(b, out);
    if (st == AAUDIO_OK && out && *out) {
        pthread_mutex_lock(&s_audio_mu);
        s_captured_stream = *out;
        s_sample_rate = s_AAudioStream_getSampleRate(*out);
        s_buffer_size = s_AAudioStream_getBufferSizeInFrames(*out);
        pthread_mutex_unlock(&s_audio_mu);
        LOGI("captured AAudio stream %p rate=%d buffer=%d ch=%d fmt=%d",
             (void *)*out, s_sample_rate, s_buffer_size, s_channels, s_fmt);
    }
    return st;
}

static aaudio_result_t hook_close_stream(AAudioStream *s) {
    pthread_mutex_lock(&s_audio_mu);
    if (s_captured_stream == s) s_captured_stream = NULL;
    pthread_mutex_unlock(&s_audio_mu);
    return s_orig_close_stream ? s_orig_close_stream(s) : s_AAudioStream_close(s);
}

static int rebuild_stream(void) {
    pthread_mutex_lock(&s_audio_mu);
    AAudioStream *old = s_captured_stream;
    s_captured_stream = NULL;
    pthread_mutex_unlock(&s_audio_mu);

    if (old) {
        aaudio_stream_state_t st = s_AAudioStream_getState(old);
        if (st != AAUDIO_STREAM_STATE_DISCONNECTED &&
            st != AAUDIO_STREAM_STATE_CLOSING &&
            st != AAUDIO_STREAM_STATE_CLOSED &&
            st != AAUDIO_STREAM_STATE_UNINITIALIZED) {
            s_AAudioStream_requestStop(old);
        }
        /* Close through the original so we don't recurse into hook_close. */
        if (s_orig_close_stream) s_orig_close_stream(old);
        else s_AAudioStream_close(old);
    }

    AAudioStreamBuilder *builder = NULL;
    if (s_AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
        LOGE("AAudio_createStreamBuilder failed during restart");
        return -1;
    }
    s_AAudioStreamBuilder_setDirection(builder, (aaudio_direction_t)s_direction);
    s_AAudioStreamBuilder_setFormat(builder, (aaudio_format_t)s_fmt);
    s_AAudioStreamBuilder_setChannelCount(builder, s_channels ? s_channels : 2);
    s_AAudioStreamBuilder_setSharingMode(builder, (aaudio_sharing_mode_t)s_sharing);
    s_AAudioStreamBuilder_setPerformanceMode(builder, (aaudio_performance_mode_t)s_perf);
    s_AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    s_AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    if (s_capacity > 0) s_AAudioStreamBuilder_setBufferCapacityInFrames(builder, s_capacity);
    if (s_game_data_cb) s_AAudioStreamBuilder_setDataCallback(builder, wrapped_data_cb, s_game_data_ud);
    s_AAudioStreamBuilder_setErrorCallback(builder, wrapped_error_cb, NULL);

    AAudioStream *neu = NULL;
    aaudio_result_t st = s_AAudioStreamBuilder_openStream(builder, &neu);
    s_AAudioStreamBuilder_delete(builder);
    if (st != AAUDIO_OK || !neu) {
        LOGE("AAudio reopen failed: %d", (int)st);
        return -1;
    }
    if (s_buffer_size > 0) s_AAudioStream_setBufferSizeInFrames(neu, s_buffer_size);
    st = s_AAudioStream_requestStart(neu);
    if (st != AAUDIO_OK) {
        LOGE("AAudio requestStart after reopen failed: %d", (int)st);
        s_AAudioStream_close(neu);
        return -1;
    }
    pthread_mutex_lock(&s_audio_mu);
    s_captured_stream = neu;
    pthread_mutex_unlock(&s_audio_mu);
    LOGI("AAudio stream recreated at %p (voices/DirectSound buffers preserved in the mixer)", (void *)neu);
    return 0;
}

static void *audio_watch_thread(void *arg) {
    (void)arg;
    for (;;) {
        usleep(250 * 1000);
        if (!atomic_exchange(&s_restart_pending, 0)) {
            pthread_mutex_lock(&s_audio_mu);
            AAudioStream *s = s_captured_stream;
            pthread_mutex_unlock(&s_audio_mu);
            if (s) {
                aaudio_stream_state_t st = s_AAudioStream_getState(s);
                if (st == AAUDIO_STREAM_STATE_DISCONNECTED ||
                    st == AAUDIO_STREAM_STATE_CLOSED ||
                    st == AAUDIO_STREAM_STATE_CLOSING) {
                    LOGW("AAudio watchdog saw state %d, reopening", (int)st);
                } else {
                    continue;
                }
            } else {
                continue;
            }
        }
        LOGI("AAudio restart requested");
        if (rebuild_stream() != 0) {
            /* Try again shortly; a brief delay helps after an incoming call. */
            atomic_store(&s_restart_pending, 1);
            usleep(500 * 1000);
        }
    }
    return NULL;
}

static void request_audio_restart(void) {
    atomic_store(&s_restart_pending, 1);
}

static void install_aaudio_hooks(void *base) {
    if (!load_aaudio()) { LOGE("AAudio hooks skipped"); return; }
    patch_got(base, "AAudioStreamBuilder_setDataCallback",
              (void *)hook_set_data_cb, (void **)&s_orig_set_data_cb);
    patch_got(base, "AAudioStreamBuilder_setFormat",
              (void *)hook_set_fmt, (void **)&s_orig_set_fmt);
    patch_got(base, "AAudioStreamBuilder_setChannelCount",
              (void *)hook_set_ch, (void **)&s_orig_set_ch);
    patch_got(base, "AAudioStreamBuilder_setPerformanceMode",
              (void *)hook_set_perf, (void **)&s_orig_set_perf);
    patch_got(base, "AAudioStreamBuilder_setSharingMode",
              (void *)hook_set_share, (void **)&s_orig_set_share);
    patch_got(base, "AAudioStreamBuilder_setBufferCapacityInFrames",
              (void *)hook_set_cap, (void **)&s_orig_set_cap);
    patch_got(base, "AAudioStreamBuilder_setDirection",
              (void *)hook_set_dir, (void **)&s_orig_set_dir);
    patch_got(base, "AAudioStreamBuilder_openStream",
              (void *)hook_open_stream, (void **)&s_orig_open_stream);
    patch_got(base, "AAudioStream_close",
              (void *)hook_close_stream, (void **)&s_orig_close_stream);

    if (!atomic_exchange(&s_audio_thread_started, 1)) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&s_audio_thread, &attr, audio_watch_thread, NULL) != 0) {
            atomic_store(&s_audio_thread_started, 0);
            LOGE("failed to start AAudio watchdog thread");
        }
        pthread_attr_destroy(&attr);
    }
    LOGI("AAudio disconnect recovery installed");
}

/* -------------------------------------------------------------------------- */
/* G4: missing Speedometer JNI + live minimap offset                          */
/* -------------------------------------------------------------------------- */

static int s_minimap_top = -1;       /* -1 unknown, 0 bottom, 1 top */
static int s_speedometer_pos = 0;

typedef void (*set_minimap_fn)(float);
static set_minimap_fn s_set_minimap;

static atomic_int s_bleach_hooked;
static void install_bleach_hook(void);

void roam_compat_apply_hud(void) {
    if (!s_set_minimap) {
        s_set_minimap = (set_minimap_fn)dlsym(RTLD_DEFAULT, "nfs_widescreen_set_minimap_offset");
    }
    if (s_minimap_top < 0) {
        const char *env = getenv("NFSU2_MINIMAP");
        if (env && (strcasecmp(env, "top") == 0 || strcmp(env, "1") == 0)) s_minimap_top = 1;
        else if (env) s_minimap_top = 0;
    }
    if (s_set_minimap && s_minimap_top >= 0) {
        s_set_minimap(s_minimap_top ? -230.0f : 0.0f);
    }
    (void)s_speedometer_pos;
    /* Bleach hook needs the dispatch table; retry until recomp_lookup hits. */
    if (!atomic_load(&s_bleach_hooked)) install_bleach_hook();
}

JNIEXPORT void JNICALL
Java_com_nfsu2_androidevolved_GameActivity_nativeSpeedometerPosition(JNIEnv *env, jclass cls, jint position) {
    (void)env; (void)cls;
    s_speedometer_pos = (int)position;
    LOGI("nativeSpeedometerPosition %d (HUD offsets applied by the prebuilt widescreen path when present)", (int)position);
}

JNIEXPORT void JNICALL
Java_io_mininxd_nfsu2_GameActivity_nativeRequestAudioRestart(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    LOGI("nativeRequestAudioRestart");
    request_audio_restart();
}

JNIEXPORT void JNICALL
Java_io_mininxd_nfsu2_GameActivity_nativeSetAudioMuted(JNIEnv *env, jclass cls, jboolean muted) {
    (void)env; (void)cls;
    atomic_store(&s_muted, muted ? 1 : 0);
    LOGI("nativeSetAudioMuted %d", muted ? 1 : 0);
    if (!muted) request_audio_restart();
}

/* Also accept the com.nfsu2 wrapper names in case Java is redirected. */
JNIEXPORT void JNICALL
Java_com_nfsu2_androidevolved_GameActivity_nativeRequestAudioRestart(JNIEnv *env, jclass cls) {
    Java_io_mininxd_nfsu2_GameActivity_nativeRequestAudioRestart(env, cls);
}
JNIEXPORT void JNICALL
Java_com_nfsu2_androidevolved_GameActivity_nativeSetAudioMuted(JNIEnv *env, jclass cls, jboolean muted) {
    Java_io_mininxd_nfsu2_GameActivity_nativeSetAudioMuted(env, cls, muted);
}

/* -------------------------------------------------------------------------- */
/* G2: break guest critical-section ABBA deadlocks (#85, #89)                 */
/*                                                                            */
/* enterCritical in the prebuilt libnfsu2_android.so (Nov 3) spins on         */
/* std::recursive_mutex::try_lock with yieldGuestMachine and has no           */
/* owner-dead / ABBA breaker (that landed later as 78d45b9 and is not in      */
/* the shipped .so). enterCritical itself is not exported, but the spin       */
/* goes through this .so's GOT to libc pthread_mutex_trylock.                 */
/*                                                                            */
/* We wrap only those GOT slots (not pthread_mutex_lock), track which         */
/* mutexes each host thread actually holds, and only return a fake success    */
/* when two threads have each been failing trylock 400+ times on a mutex      */
/* the other holds — the exact ABBA pattern in the #85/#89 logs               */
/* (threads 1 and 6 on CS 0x00864f00 and 0x10806348). The matching            */
/* pthread_mutex_unlock is a no-op for a fake acquire, so we never unlock     */
/* a mutex this thread does not own. Only one side of a cycle is broken       */
/* at a time. If the pattern does not match, behaviour is unchanged.          */
/* -------------------------------------------------------------------------- */

#define CS_MAX_THREADS 32
#define CS_MAX_HELD    16
#define CS_BREAK_AFTER 400u

struct cs_thr {
    pthread_t tid;
    int in_use;
    unsigned fail_count;
    void *failing_on;
    void *held[CS_MAX_HELD];
    unsigned held_n[CS_MAX_HELD];
    void *fake[CS_MAX_HELD];
    unsigned fake_n[CS_MAX_HELD];
};

/* Each guest/host thread writes only its own slot. The common unlock and
 * uncontended trylock paths take no lock. The ABBA scan (only after 400
 * failures) reads other slots; those fields are stored atomically. */
static struct cs_thr s_cs_thr[CS_MAX_THREADS];
static atomic_int s_cs_used[CS_MAX_THREADS];
static _Thread_local struct cs_thr *s_cs_me;
static atomic_int s_cs_breaking;
static int (*s_orig_trylock)(pthread_mutex_t *);
static int (*s_orig_unlock)(pthread_mutex_t *);

static struct cs_thr *cs_mine(void) {
    if (s_cs_me) return s_cs_me;
    pthread_t self = pthread_self();
    for (int i = 0; i < CS_MAX_THREADS; i++) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&s_cs_used[i], &expected, 1)) {
            memset(&s_cs_thr[i], 0, sizeof(s_cs_thr[i]));
            s_cs_thr[i].tid = self;
            s_cs_thr[i].in_use = 1;
            s_cs_me = &s_cs_thr[i];
            return s_cs_me;
        }
        /* Lost the slot; if it is ours (thread restart) reuse it. */
        if (s_cs_thr[i].in_use && pthread_equal(s_cs_thr[i].tid, self)) {
            s_cs_me = &s_cs_thr[i];
            return s_cs_me;
        }
    }
    return NULL;
}

static int cs_idx(void **arr, unsigned *cnt, int cap, void *m, int alloc) {
    int empty = -1;
    for (int i = 0; i < cap; i++) {
        if (arr[i] == m) return i;
        if (!arr[i] && empty < 0) empty = i;
        (void)cnt;
    }
    return alloc ? empty : -1;
}

static void cs_hold(struct cs_thr *t, void *m) {
    int i = cs_idx(t->held, t->held_n, CS_MAX_HELD, m, 1);
    if (i < 0) return;
    if (!t->held[i]) { t->held[i] = m; t->held_n[i] = 1; }
    else t->held_n[i]++;
}

static void cs_release(struct cs_thr *t, void *m) {
    int i = cs_idx(t->held, t->held_n, CS_MAX_HELD, m, 0);
    if (i < 0) return;
    if (t->held_n[i] > 1) t->held_n[i]--;
    else { t->held[i] = NULL; t->held_n[i] = 0; }
}

static int cs_holds(const struct cs_thr *t, void *m) {
    int i = cs_idx((void **)t->held, (unsigned *)t->held_n, CS_MAX_HELD, m, 0);
    return i >= 0 && t->held_n[i] > 0;
}

static int cs_fake_inc(struct cs_thr *t, void *m) {
    int i = cs_idx(t->fake, t->fake_n, CS_MAX_HELD, m, 1);
    if (i < 0) return 0;
    if (!t->fake[i]) { t->fake[i] = m; t->fake_n[i] = 1; }
    else t->fake_n[i]++;
    return 1;
}

static int cs_fake_owned(const struct cs_thr *t, void *m) {
    int i = cs_idx((void **)t->fake, (unsigned *)t->fake_n, CS_MAX_HELD, m, 0);
    return i >= 0 && t->fake_n[i] > 0;
}

static int cs_fake_dec(struct cs_thr *t, void *m) {
    int i = cs_idx(t->fake, t->fake_n, CS_MAX_HELD, m, 0);
    if (i < 0) return 0;
    if (t->fake_n[i] > 1) { t->fake_n[i]--; return 1; }
    t->fake[i] = NULL;
    t->fake_n[i] = 0;
    return 1;
}

static int cs_any_fake(void) {
    for (int i = 0; i < CS_MAX_THREADS; i++) {
        if (!s_cs_thr[i].in_use) continue;
        for (int j = 0; j < CS_MAX_HELD; j++)
            if (s_cs_thr[i].fake_n[j]) return 1;
    }
    return 0;
}

static int hook_pthread_mutex_trylock(pthread_mutex_t *m) {
    int r = s_orig_trylock ? s_orig_trylock(m) : pthread_mutex_trylock(m);
    struct cs_thr *me = cs_mine();
    if (!me) return r;
    if (r == 0) {
        cs_hold(me, m);
        me->fail_count = 0;
        me->failing_on = NULL;
        return 0;
    }
    /* Already loaned this mutex to ourselves (recursive Enter after a break). */
    if (cs_fake_owned(me, m)) {
        cs_fake_inc(me, m);
        return 0;
    }
    me->fail_count++;
    me->failing_on = m;

    if (me->fail_count >= CS_BREAK_AFTER && !atomic_load(&s_cs_breaking)) {
        for (int i = 0; i < CS_MAX_THREADS; i++) {
            struct cs_thr *ot = &s_cs_thr[i];
            if (!ot->in_use || ot == me) continue;
            if (ot->fail_count < CS_BREAK_AFTER) continue;
            if (!ot->failing_on) continue;
            /* ABBA: I hold what they wait on, they hold what I wait on. */
            if (cs_holds(me, ot->failing_on) && cs_holds(ot, m)) {
                int expected = 0;
                if (atomic_compare_exchange_strong(&s_cs_breaking, &expected, 1)) {
                    if (cs_fake_inc(me, m)) {
                        LOGW("G2: breaking guest CS ABBA deadlock "
                             "(this=%p waits on %p held by %p, who waits on %p); "
                             "loaning a bypass, unlock will be a no-op (refs #85 #89)",
                             (void *)me, m, (void *)ot, ot->failing_on);
                        me->fail_count = 0;
                        me->failing_on = NULL;
                        return 0;
                    }
                    atomic_store(&s_cs_breaking, 0);
                }
                break;
            }
        }
    }
    return r;
}

static int hook_pthread_mutex_unlock(pthread_mutex_t *m) {
    struct cs_thr *me = s_cs_me; /* never allocate on the unlock fast path */
    if (me) {
        if (atomic_load(&s_cs_breaking) && cs_fake_dec(me, m)) {
            if (!cs_any_fake()) atomic_store(&s_cs_breaking, 0);
            return 0;
        }
        cs_release(me, m);
    }
    return s_orig_unlock ? s_orig_unlock(m) : pthread_mutex_unlock(m);
}

static void install_cs_deadlock_hooks(void *base) {
    /* libc primitives: every same-.so recursive_mutex::try_lock ends here
     * through the PLT, whether or not the C++ wrapper is inlined. */
    if (patch_got(base, "pthread_mutex_trylock",
                  (void *)hook_pthread_mutex_trylock, (void **)&s_orig_trylock) == 0)
        LOGI("G2: hooked pthread_mutex_trylock for guest CS ABBA breaker");
    if (patch_got(base, "pthread_mutex_unlock",
                  (void *)hook_pthread_mutex_unlock, (void **)&s_orig_unlock) == 0)
        LOGI("G2: hooked pthread_mutex_unlock (fake-acquire no-op only)");
}

/* -------------------------------------------------------------------------- */
/* G6: skip the Enhanced Contrast 3-pass blit (#70)                           */
/*                                                                            */
/* The source already has ov_bleach_bypass in overrides.c (SetGammaRamp       */
/* instead of a 3-pass full-screen LockRect pipeline), but that override is   */
/* not in the prebuilt libnfsu2_android.so, and overrides.txt never listed    */
/* 0x005C68D0 so even a rebuilt runtime would still lift the original body.   */
/* libgraphics_cache.so is rebuilt by CI and already applies the S-curve      */
/* via nfs_contrast_sync_ingame on toggle; we just have to stop the guest     */
/* pass from still running. recomp_lookup(0x005C68D0) is exported, so we      */
/* can ARM64-hook the lifted entry and return immediately after marking       */
/* the post-process state complete (same 0x008708F4 write as ov_bleach).      */
/* Gamma is NOT re-applied every frame — only on change, in the Rust sync.    */
/* -------------------------------------------------------------------------- */

static uint32_t *s_g_esp;
static uintptr_t *s_g_mem; /* g_mem_base, a pointer-sized guest base */

static void hook_bleach_bypass(void) {
    /* cdecl, no args: pop the return slot. */
    if (s_g_esp) *s_g_esp += 4;
    if (s_g_mem && *s_g_mem) {
        uint32_t *state = (uint32_t *)((uint8_t *)(*s_g_mem) + 0x008708F4u);
        if (*state != 0xFFFFFFFFu) *state = 0xFFFFFFFFu;
    }
}

static void install_bleach_hook(void) {
    if (atomic_load(&s_bleach_hooked)) return;
    typedef void (*recomp_func_t)(void);
    recomp_func_t (*lookup)(uint32_t) = (recomp_func_t (*)(uint32_t))dlsym(RTLD_DEFAULT, "recomp_lookup");
    if (!lookup) {
        void *h = dlopen("libnfsu2_android.so", RTLD_NOLOAD);
        if (h) lookup = (recomp_func_t (*)(uint32_t))dlsym(h, "recomp_lookup");
    }
    if (!lookup) {
        LOGW("G6: recomp_lookup not found; cannot hook bleach pass");
        return;
    }
    if (!s_g_esp) {
        s_g_esp = (uint32_t *)dlsym(RTLD_DEFAULT, "g_esp");
        s_g_mem = (uintptr_t *)dlsym(RTLD_DEFAULT, "g_mem_base");
    }
    void *fn = (void *)lookup(0x005C68D0u);
    if (!fn) {
        static atomic_int logged;
        if (!atomic_exchange(&logged, 1))
            LOGW("G6: recomp_lookup(0x005C68D0) returned null (will retry quietly)");
        return;
    }
    if (!arm64_hook(fn, (void *)hook_bleach_bypass)) {
        LOGE("G6: ARM64 hook of bleach pass at %p failed", fn);
        return;
    }
    atomic_store(&s_bleach_hooked, 1);
    LOGI("G6: hooked lifted 0x005C68D0 at %p — 3-pass contrast blit skipped, "
         "tone stays on SetGammaRamp (refs #70)", fn);
}

/* -------------------------------------------------------------------------- */
/* Install                                                                   */
/* -------------------------------------------------------------------------- */

static atomic_int s_installed;

void roam_compat_install(void) {
    if (atomic_exchange(&s_installed, 1)) return;
    void *base = game_base();
    if (!base) {
        atomic_store(&s_installed, 0);
        LOGW("libnfsu2_android.so not loaded yet; will retry");
        return;
    }
    LOGI("installing compat hooks (game base %p, android user %d)", base, android_user_id());
    install_icall_hook();
    install_path_hooks(base);
    install_aaudio_hooks(base);
    install_cs_deadlock_hooks(base);
    install_bleach_hook();
    roam_compat_apply_hud();
    LOGI("compat hooks ready");
}

__attribute__((constructor))
static void roam_compat_ctor(void) {
    /* nfsu2_android is loaded first (GameActivity static block), so the
     * constructor of nfsu2_roam already sees its symbols. */
    roam_compat_install();
}
