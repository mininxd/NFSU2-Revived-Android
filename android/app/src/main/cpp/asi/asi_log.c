#define _GNU_SOURCE
#include "asi_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <android/log.h>

#define ASI_LOG_TAG "NFSU2-ASI"
#define API_RING_SIZE 16

typedef struct {
    char     label[96];
    uint32_t caller_eip;
} ApiTraceEntry;

static FILE            *s_log_fp = NULL;
static int              s_log_fd = -1;
static pthread_mutex_t  s_log_mu = PTHREAD_MUTEX_INITIALIZER;
static char             s_log_path[1024] = {0};

static ApiTraceEntry    s_api_ring[API_RING_SIZE];
static uint32_t         s_api_ring_pos = 0;

static volatile int     s_in_asi_ctx = 0;
static char             s_ctx_mod[128] = {0};
static volatile uint32_t s_ctx_fn_va = 0;
static volatile uint32_t s_ctx_eip = 0;
static uint8_t *volatile s_ctx_mem_base = NULL;

static struct sigaction s_prev_segv;
static struct sigaction s_prev_bus;
static struct sigaction s_prev_ill;
static struct sigaction s_prev_abrt;
static struct sigaction s_prev_fpe;
static int              s_sig_installed = 0;

static void write_raw_fd(int fd, const char *s) {
    if (fd < 0 || !s) return;
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0) break;
        s += n;
        len -= (size_t)n;
    }
}

static void fmt_u32_hex(char *out, uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++) {
        out[2 + i] = hx[(v >> (28 - i * 4)) & 0xFu];
    }
    out[10] = '\0';
}

static void fmt_u64_hex(char *out, uint64_t v) {
    static const char hx[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = hx[(v >> (60 - i * 4)) & 0xFu];
    }
    out[18] = '\0';
}

static void asi_crash_handler(int sig, siginfo_t *info, void *uctx) {
    if (s_log_fd >= 0 && s_in_asi_ctx) {
        char buf[32];
        write_raw_fd(s_log_fd, "\n================ FATAL CRASH IN ASI/MODLOADER ================\n");
        write_raw_fd(s_log_fd, "Signal: ");
        if (sig == SIGSEGV) write_raw_fd(s_log_fd, "SIGSEGV");
        else if (sig == SIGBUS) write_raw_fd(s_log_fd, "SIGBUS");
        else if (sig == SIGILL) write_raw_fd(s_log_fd, "SIGILL");
        else if (sig == SIGABRT) write_raw_fd(s_log_fd, "SIGABRT");
        else if (sig == SIGFPE) write_raw_fd(s_log_fd, "SIGFPE");
        else write_raw_fd(s_log_fd, "UNKNOWN");
        write_raw_fd(s_log_fd, "\nActive Mod: ");
        write_raw_fd(s_log_fd, s_ctx_mod[0] ? s_ctx_mod : "(unknown)");
        write_raw_fd(s_log_fd, "\nActive Guest Fn: ");
        fmt_u32_hex(buf, s_ctx_fn_va);
        write_raw_fd(s_log_fd, buf);
        write_raw_fd(s_log_fd, "  Last EIP: ");
        fmt_u32_hex(buf, s_ctx_eip);
        write_raw_fd(s_log_fd, buf);
        if (info) {
            write_raw_fd(s_log_fd, "\nFault Host Addr: ");
            fmt_u64_hex(buf, (uint64_t)(uintptr_t)info->si_addr);
            write_raw_fd(s_log_fd, buf);
            if (s_ctx_mem_base && (uintptr_t)info->si_addr >= (uintptr_t)s_ctx_mem_base) {
                uint64_t delta = (uint64_t)((uintptr_t)info->si_addr - (uintptr_t)s_ctx_mem_base);
                if (delta < 0x100000000ULL) {
                    write_raw_fd(s_log_fd, "  (Guest VA: ");
                    fmt_u32_hex(buf, (uint32_t)delta);
                    write_raw_fd(s_log_fd, buf);
                    write_raw_fd(s_log_fd, ")");
                }
            }
        }
        write_raw_fd(s_log_fd, "\nRecent HLE API Calls:\n");
        uint32_t pos = s_api_ring_pos;
        for (uint32_t i = 0; i < API_RING_SIZE; i++) {
            uint32_t idx = (pos + i) % API_RING_SIZE;
            if (s_api_ring[idx].label[0]) {
                write_raw_fd(s_log_fd, "  - ");
                write_raw_fd(s_log_fd, s_api_ring[idx].label);
                write_raw_fd(s_log_fd, "  @ ");
                fmt_u32_hex(buf, s_api_ring[idx].caller_eip);
                write_raw_fd(s_log_fd, buf);
                write_raw_fd(s_log_fd, "\n");
            }
        }
        write_raw_fd(s_log_fd, "==============================================================\n");
        fsync(s_log_fd);
    }

    struct sigaction *prev = NULL;
    if (sig == SIGSEGV) prev = &s_prev_segv;
    else if (sig == SIGBUS) prev = &s_prev_bus;
    else if (sig == SIGILL) prev = &s_prev_ill;
    else if (sig == SIGABRT) prev = &s_prev_abrt;
    else if (sig == SIGFPE) prev = &s_prev_fpe;

    if (prev && (prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction) {
        prev->sa_sigaction(sig, info, uctx);
        return;
    }
    if (prev && !(prev->sa_flags & SA_SIGINFO) && prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void asi_log_vwrite(int prio, const char *lvl, const char *fmt, va_list ap) {
    char msg[1536];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    __android_log_print(prio, ASI_LOG_TAG, "%s", msg);

    pthread_mutex_lock(&s_log_mu);
    if (s_log_fp) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tmv;
        localtime_r(&ts.tv_sec, &tmv);
        fprintf(s_log_fp, "[%02d:%02d:%02d.%03ld] [%s] %s\n",
                tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L, lvl, msg);
        fflush(s_log_fp);
    }
    pthread_mutex_unlock(&s_log_mu);
}

void asi_log_init(const char *game_dir) {
    pthread_mutex_lock(&s_log_mu);
    if (s_log_fp) {
        fclose(s_log_fp);
        s_log_fp = NULL;
        s_log_fd = -1;
    }
    memset(s_api_ring, 0, sizeof(s_api_ring));
    s_api_ring_pos = 0;

    if (game_dir && game_dir[0]) {
        snprintf(s_log_path, sizeof(s_log_path), "%s/dinput8_log.txt", game_dir);
        s_log_fp = fopen(s_log_path, "w");
        if (s_log_fp) {
            s_log_fd = fileno(s_log_fp);
            setvbuf(s_log_fp, NULL, _IOLBF, 0);
            fprintf(s_log_fp, "==============================================================\n");
            fprintf(s_log_fp, "NFSU2 Revived Android - dinput8 ASI Modloader Log (v1.1.beta1)\n");
            fprintf(s_log_fp, "Engine: Unicorn x86-32 -> ARM64 Selective JIT + Win32/UCRT HLE\n");
            fprintf(s_log_fp, "Game Dir: %s\n", game_dir);
            fprintf(s_log_fp, "==============================================================\n");
            fflush(s_log_fp);
        }
    }

    if (!s_sig_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = asi_crash_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, &s_prev_segv);
        sigaction(SIGBUS,  &sa, &s_prev_bus);
        sigaction(SIGILL,  &sa, &s_prev_ill);
        sigaction(SIGABRT, &sa, &s_prev_abrt);
        sigaction(SIGFPE,  &sa, &s_prev_fpe);
        s_sig_installed = 1;
    }
    pthread_mutex_unlock(&s_log_mu);
}

void asi_log_close(void) {
    pthread_mutex_lock(&s_log_mu);
    if (s_log_fp) {
        fflush(s_log_fp);
        if (s_log_fd >= 0) fsync(s_log_fd);
        fclose(s_log_fp);
        s_log_fp = NULL;
        s_log_fd = -1;
    }
    pthread_mutex_unlock(&s_log_mu);
}

void asi_log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    asi_log_vwrite(ANDROID_LOG_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void asi_log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    asi_log_vwrite(ANDROID_LOG_WARN, "WARN", fmt, ap);
    va_end(ap);
}

void asi_log_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    asi_log_vwrite(ANDROID_LOG_ERROR, "ERR ", fmt, ap);
    va_end(ap);
}

void asi_log_trace_api(const char *dll, const char *fn, uint32_t caller_eip) {
    uint32_t idx = s_api_ring_pos % API_RING_SIZE;
    snprintf(s_api_ring[idx].label, sizeof(s_api_ring[idx].label), "%s!%s",
             dll ? dll : "?", fn ? fn : "?");
    s_api_ring[idx].caller_eip = caller_eip;
    s_api_ring_pos++;
}

void asi_log_set_context(const char *mod_name, uint32_t fn_va, uint32_t last_eip, uint8_t *mem_base) {
    if (mod_name) {
        strncpy(s_ctx_mod, mod_name, sizeof(s_ctx_mod) - 1);
        s_ctx_mod[sizeof(s_ctx_mod) - 1] = '\0';
    } else {
        s_ctx_mod[0] = '\0';
    }
    s_ctx_fn_va = fn_va;
    s_ctx_eip = last_eip;
    s_ctx_mem_base = mem_base;
    s_in_asi_ctx = 1;
}

void asi_log_clear_context(void) {
    s_in_asi_ctx = 0;
}

void asi_log_dump_fault(const char *reason, const AsiX86Regs *regs,
                        const uint8_t *mem_base, const char *mod_desc) {
    asi_log_err("---- X86 FAULT: %s ----", reason ? reason : "unknown");
    if (mod_desc && mod_desc[0]) {
        asi_log_err("Location: %s", mod_desc);
    }
    if (regs) {
        asi_log_err("EIP=0x%08X  EFLAGS=0x%08X", regs->eip, regs->eflags);
        asi_log_err("EAX=0x%08X  EBX=0x%08X  ECX=0x%08X  EDX=0x%08X",
                    regs->eax, regs->ebx, regs->ecx, regs->edx);
        asi_log_err("ESI=0x%08X  EDI=0x%08X  EBP=0x%08X  ESP=0x%08X",
                    regs->esi, regs->edi, regs->ebp, regs->esp);
        if (mem_base && regs->esp >= 0x00010000u && regs->esp < 0x7FFF0000u) {
            const uint32_t *stk = (const uint32_t *)(mem_base + regs->esp);
            asi_log_err("Stack [ESP+00..1C]: %08X %08X %08X %08X  %08X %08X %08X %08X",
                        stk[0], stk[1], stk[2], stk[3], stk[4], stk[5], stk[6], stk[7]);
        }
    }
    pthread_mutex_lock(&s_log_mu);
    if (s_log_fp) {
        fprintf(s_log_fp, "Recent HLE API calls:\n");
        for (uint32_t i = 0; i < API_RING_SIZE; i++) {
            uint32_t idx = (s_api_ring_pos + i) % API_RING_SIZE;
            if (s_api_ring[idx].label[0]) {
                fprintf(s_log_fp, "  [%02u] %s  (from 0x%08X)\n",
                        i, s_api_ring[idx].label, s_api_ring[idx].caller_eip);
            }
        }
        fflush(s_log_fp);
        if (s_log_fd >= 0) fsync(s_log_fd);
    }
    pthread_mutex_unlock(&s_log_mu);
}
