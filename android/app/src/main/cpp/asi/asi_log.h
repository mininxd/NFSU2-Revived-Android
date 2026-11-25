#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
} AsiX86Regs;

/* Open/truncate <game_dir>/dinput8_log.txt and install crash signal hooks */
void asi_log_init(const char *game_dir);
void asi_log_close(void);

/* Structured logging (always mirrored to Android logcat under NFSU2-ASI) */
void asi_log_info(const char *fmt, ...);
void asi_log_warn(const char *fmt, ...);
void asi_log_err(const char *fmt, ...);

/* Record recent Win32/UCRT HLE calls into a crash ring buffer */
void asi_log_trace_api(const char *dll, const char *fn, uint32_t caller_eip);

/* Update active execution context so a fatal signal prints the offending mod/function */
void asi_log_set_context(const char *mod_name, uint32_t fn_va, uint32_t last_eip, uint8_t *mem_base);
void asi_log_clear_context(void);

/* Dump x86 registers, guest stack, and recent API trace into dinput8_log.txt */
void asi_log_dump_fault(const char *reason, const AsiX86Regs *regs,
                        const uint8_t *mem_base, const char *mod_desc);

#ifdef __cplusplus
}
#endif
