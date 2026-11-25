#define _GNU_SOURCE
#include "asi_win32.h"
#include "asi_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>

/* =========================================================================
 * Guest Memory Helpers
 * ========================================================================= */
static AsiWin32Host s_host;
static uint8_t     *s_mem = NULL;

static inline uint32_t g_rd32(uint32_t va) {
    uint32_t v = 0;
    if (s_mem && va >= 0x1000u && va < 0xFFFFFFFCu) memcpy(&v, s_mem + va, 4);
    return v;
}
static inline void g_wr32(uint32_t va, uint32_t v) {
    if (s_mem && va >= 0x1000u && va < 0xFFFFFFFCu) memcpy(s_mem + va, &v, 4);
}
static inline uint16_t g_rd16(uint32_t va) {
    uint16_t v = 0;
    if (s_mem && va >= 0x1000u && va < 0xFFFFFFFEu) memcpy(&v, s_mem + va, 2);
    return v;
}
static inline void g_wr16(uint32_t va, uint16_t v) {
    if (s_mem && va >= 0x1000u && va < 0xFFFFFFFEu) memcpy(s_mem + va, &v, 2);
}
static inline uint8_t *g_ptr(uint32_t va) {
    if (!s_mem || va < 0x1000u) return NULL;
    return s_mem + va;
}
static inline const char *g_cstr(uint32_t va) {
    if (!s_mem || va < 0x1000u) return NULL;
    return (const char *)(s_mem + va);
}
static inline uint32_t g_arg(const AsiCallRegs *r, int idx) {
    return g_rd32(r->esp + 4u + (uint32_t)idx * 4u);
}

/* =========================================================================
 * Isolated Guest ASI Heap (bump + free-list inside ASI_HEAP_ARENA)
 * ========================================================================= */
typedef struct {
    uint32_t va;
    uint32_t size;
    int      in_use;
} AsiHeapBlock;

#define ASI_HEAP_MAX_BLOCKS 8192
static AsiHeapBlock    s_heap_blocks[ASI_HEAP_MAX_BLOCKS];
static uint32_t        s_heap_block_count = 0;
static uint32_t        s_heap_cursor = ASI_HEAP_ARENA;
static pthread_mutex_t s_heap_mu = PTHREAD_MUTEX_INITIALIZER;

uint32_t asi_heap_alloc(uint32_t size, int zero_fill) {
    if (!s_mem) return 0;
    if (size == 0) size = 16;
    uint32_t aligned = (size + 15u) & ~15u;

    pthread_mutex_lock(&s_heap_mu);
    for (uint32_t i = 0; i < s_heap_block_count; i++) {
        if (!s_heap_blocks[i].in_use && s_heap_blocks[i].size >= aligned &&
            s_heap_blocks[i].size <= aligned * 2u + 64u) {
            s_heap_blocks[i].in_use = 1;
            uint32_t va = s_heap_blocks[i].va;
            pthread_mutex_unlock(&s_heap_mu);
            if (zero_fill) memset(s_mem + va, 0, size);
            return va;
        }
    }
    if (s_heap_cursor + aligned > ASI_HEAP_ARENA + ASI_HEAP_ARENA_SZ ||
        s_heap_block_count >= ASI_HEAP_MAX_BLOCKS) {
        pthread_mutex_unlock(&s_heap_mu);
        asi_log_err("ASI private heap exhausted (req=%u)", size);
        return 0;
    }
    uint32_t va = s_heap_cursor;
    s_heap_cursor += aligned;
    s_heap_blocks[s_heap_block_count].va = va;
    s_heap_blocks[s_heap_block_count].size = aligned;
    s_heap_blocks[s_heap_block_count].in_use = 1;
    s_heap_block_count++;
    pthread_mutex_unlock(&s_heap_mu);

    if (zero_fill) memset(s_mem + va, 0, aligned);
    return va;
}

uint32_t asi_heap_size(uint32_t va) {
    pthread_mutex_lock(&s_heap_mu);
    for (uint32_t i = 0; i < s_heap_block_count; i++) {
        if (s_heap_blocks[i].va == va && s_heap_blocks[i].in_use) {
            uint32_t sz = s_heap_blocks[i].size;
            pthread_mutex_unlock(&s_heap_mu);
            return sz;
        }
    }
    pthread_mutex_unlock(&s_heap_mu);
    return 0;
}

uint32_t asi_heap_realloc(uint32_t va, uint32_t new_size) {
    if (va == 0) return asi_heap_alloc(new_size, 0);
    if (new_size == 0) {
        asi_heap_free(va);
        return 0;
    }
    uint32_t old_sz = asi_heap_size(va);
    if (old_sz >= new_size) return va;
    uint32_t nv = asi_heap_alloc(new_size, 0);
    if (nv && old_sz > 0) {
        memcpy(s_mem + nv, s_mem + va, old_sz < new_size ? old_sz : new_size);
    }
    asi_heap_free(va);
    return nv;
}

void asi_heap_free(uint32_t va) {
    if (!va) return;
    pthread_mutex_lock(&s_heap_mu);
    for (uint32_t i = 0; i < s_heap_block_count; i++) {
        if (s_heap_blocks[i].va == va) {
            s_heap_blocks[i].in_use = 0;
            break;
        }
    }
    pthread_mutex_unlock(&s_heap_mu);
}

/* =========================================================================
 * Path Mapping (Windows -> Android Game Dir)
 * ========================================================================= */
static void map_win_path(const char *win_path, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!win_path || !win_path[0]) {
        snprintf(out, out_sz, "%s", s_host.game_dir ? s_host.game_dir : "/sdcard/nfsu2");
        return;
    }

    char tmp[1024];
    size_t len = strlen(win_path);
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    for (size_t i = 0; i < len; i++) {
        tmp[i] = (win_path[i] == '\\') ? '/' : win_path[i];
    }
    tmp[len] = '\0';

    const char *p = tmp;
    if (strncasecmp(p, "C:/nfsu2/", 9) == 0) {
        snprintf(out, out_sz, "%s/%s", s_host.game_dir, p + 9);
        return;
    }
    if (strncasecmp(p, "C:/nfsu2", 8) == 0 && p[8] == '\0') {
        snprintf(out, out_sz, "%s", s_host.game_dir);
        return;
    }
    if (p[0] == '/') {
        snprintf(out, out_sz, "%s", p);
        return;
    }
    if (p[0] == '.' && p[1] == '/') p += 2;
    snprintf(out, out_sz, "%s/%s", s_host.game_dir, p);
}

/* Case-insensitive file open helper for Android case-sensitive filesystems */
static void resolve_case_path(const char *in_path, char *out, size_t out_sz) {
    struct stat st;
    if (stat(in_path, &st) == 0) {
        snprintf(out, out_sz, "%s", in_path);
        return;
    }
    /* Try matching basename case-insensitively inside parent directory */
    const char *slash = strrchr(in_path, '/');
    if (slash && slash > in_path) {
        char dir[1024];
        size_t dlen = (size_t)(slash - in_path);
        if (dlen < sizeof(dir)) {
            memcpy(dir, in_path, dlen);
            dir[dlen] = '\0';
            const char *base = slash + 1;
            DIR *dp = opendir(dir);
            if (dp) {
                struct dirent *de;
                while ((de = readdir(dp)) != NULL) {
                    if (strcasecmp(de->d_name, base) == 0) {
                        snprintf(out, out_sz, "%s/%s", dir, de->d_name);
                        closedir(dp);
                        return;
                    }
                }
                closedir(dp);
            }
        }
    }
    snprintf(out, out_sz, "%s", in_path);
}

/* =========================================================================
 * Win32 Handle Tables (Files, FindFirst, CRT FILE*, TLS)
 * ========================================================================= */
#define MAX_WIN_FILES   128
#define MAX_FIND_HND    32
#define MAX_CRT_FILES   64
#define MAX_TLS_SLOTS   128

typedef struct {
    int   in_use;
    FILE *fp;
} WinFileHandle;

typedef struct {
    int   in_use;
    DIR  *dp;
    char  dir_path[1024];
    char  pattern[256];
} WinFindHandle;

typedef struct {
    int   in_use;
    FILE *fp;
    uint32_t guest_struct_va;
} CrtFileSlot;

static WinFileHandle s_win_files[MAX_WIN_FILES];
static WinFindHandle s_find_handles[MAX_FIND_HND];
static CrtFileSlot   s_crt_files[MAX_CRT_FILES];
static uint32_t      s_tls_values[MAX_TLS_SLOTS];
static uint8_t       s_tls_used[MAX_TLS_SLOTS];
static uint32_t      s_last_error = 0;
static uint32_t      s_crt_errno_va = 0;
static uint32_t      s_crt_iob_va = 0;
static uint32_t      s_cmdline_a_va = 0;
static uint32_t      s_cmdline_w_va = 0;
static uint32_t      s_locale_va = 0;

static int wild_match(const char *pat, const char *str) {
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            while (*str) {
                if (wild_match(pat, str)) return 1;
                str++;
            }
            return 0;
        }
        if (*pat == '?' || tolower((unsigned char)*pat) == tolower((unsigned char)*str)) {
            if (!*str) return 0;
            pat++;
            str++;
        } else {
            return 0;
        }
    }
    return *str == '\0';
}

/* =========================================================================
 * INI Reader / Writer (GetPrivateProfileIntA / StringA / WritePrivateProfileStringA)
 * ========================================================================= */
static int ini_read_value(const char *host_path, const char *section, const char *key,
                          char *out, size_t out_sz) {
    char resolved[1024];
    resolve_case_path(host_path, resolved, sizeof(resolved));
    FILE *fp = fopen(resolved, "r");
    if (!fp) return 0;

    char line[1024];
    int in_sec = (section == NULL || section[0] == '\0');
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == ';' || *p == '#' || *p == '\r' || *p == '\n') continue;
        if (*p == '[') {
            char *end = strchr(p + 1, ']');
            if (end) {
                *end = '\0';
                in_sec = (section && strcasecmp(p + 1, section) == 0);
            }
            continue;
        }
        if (!in_sec) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = p;
        char *ke = eq - 1;
        while (ke >= k && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';
        if (strcasecmp(k, key) != 0) continue;

        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        /* Strip trailing comments (// or ;) and whitespace */
        char *semi = strchr(v, ';');
        if (semi) *semi = '\0';
        char *slash2 = strstr(v, "//");
        if (slash2) *slash2 = '\0';
        size_t vlen = strlen(v);
        while (vlen > 0 && (v[vlen - 1] == '\r' || v[vlen - 1] == '\n' ||
                            v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) {
            v[--vlen] = '\0';
        }
        if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"') {
            v[vlen - 1] = '\0';
            v++;
        }
        snprintf(out, out_sz, "%s", v);
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}

/* =========================================================================
 * UTF-16LE <-> ASCII helpers
 * ========================================================================= */
static void utf16_to_ascii(uint32_t wva, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!wva || !s_mem) return;
    size_t i = 0;
    while (i + 1 < out_sz) {
        uint16_t ch = g_rd16(wva + (uint32_t)i * 2u);
        if (ch == 0) break;
        out[i++] = (ch < 128u) ? (char)ch : '?';
    }
    out[i] = '\0';
}

static uint32_t ascii_to_utf16(const char *src, uint32_t dst_va, uint32_t max_chars) {
    if (!src || !dst_va || max_chars == 0) return 0;
    uint32_t i = 0;
    while (src[i] && i + 1 < max_chars) {
        g_wr16(dst_va + i * 2u, (uint8_t)src[i]);
        i++;
    }
    g_wr16(dst_va + i * 2u, 0);
    return i;
}

/* =========================================================================
 * Printf formatter from guest x86 stack va_list -> host buffer
 * ========================================================================= */
static int guest_vsnprintf(char *out, size_t out_sz, const char *fmt, uint32_t va_list_ptr) {
    if (!out || out_sz == 0) return 0;
    if (!fmt) { out[0] = '\0'; return 0; }

    size_t pos = 0;
    uint32_t ap = va_list_ptr;
    for (const char *p = fmt; *p && pos + 1 < out_sz; ) {
        if (*p != '%') {
            out[pos++] = *p++;
            continue;
        }
        p++;
        if (*p == '%') {
            out[pos++] = '%';
            p++;
            continue;
        }
        char spec[32];
        size_t slen = 0;
        spec[slen++] = '%';
        int is_long_long = 0;
        while (*p && slen < sizeof(spec) - 2 &&
               strchr("-+ #0123456789.*lhLzI", *p)) {
            if (*p == '*' && ap) {
                int w = (int)g_rd32(ap); ap += 4;
                int n = snprintf(spec + slen, sizeof(spec) - slen - 2, "%d", w);
                if (n > 0) slen += (size_t)n;
                p++;
                continue;
            }
            if (p[0] == 'l' && p[1] == 'l') is_long_long = 1;
            if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
                is_long_long = 1;
                spec[slen++] = 'l';
                spec[slen++] = 'l';
                p += 3;
                continue;
            }
            spec[slen++] = *p++;
        }
        char conv = *p ? *p++ : '\0';
        if (!conv) break;
        spec[slen++] = conv;
        spec[slen] = '\0';

        char tmp[256];
        int n = 0;
        if (conv == 's') {
            uint32_t sva = g_rd32(ap); ap += 4;
            const char *s = sva ? g_cstr(sva) : "(null)";
            n = snprintf(tmp, sizeof(tmp), spec, s ? s : "(null)");
        } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                   conv == 'g' || conv == 'G') {
            double d = 0.0;
            uint32_t lo = g_rd32(ap), hi = g_rd32(ap + 4);
            ap += 8;
            uint64_t bits = ((uint64_t)hi << 32) | lo;
            memcpy(&d, &bits, 8);
            n = snprintf(tmp, sizeof(tmp), spec, d);
        } else if (conv == 'p') {
            uint32_t v = g_rd32(ap); ap += 4;
            n = snprintf(tmp, sizeof(tmp), "0x%08X", v);
        } else if (is_long_long) {
            uint32_t lo = g_rd32(ap), hi = g_rd32(ap + 4);
            ap += 8;
            uint64_t v = ((uint64_t)hi << 32) | lo;
            n = snprintf(tmp, sizeof(tmp), spec, v);
        } else if (conv == 'd' || conv == 'i') {
            int32_t v = (int32_t)g_rd32(ap); ap += 4;
            n = snprintf(tmp, sizeof(tmp), spec, v);
        } else if (conv == 'c') {
            int v = (int)g_rd32(ap); ap += 4;
            n = snprintf(tmp, sizeof(tmp), spec, v);
        } else {
            uint32_t v = g_rd32(ap); ap += 4;
            n = snprintf(tmp, sizeof(tmp), spec, v);
        }
        if (n > 0) {
            size_t avail = out_sz - 1 - pos;
            size_t copy = (size_t)n < avail ? (size_t)n : avail;
            memcpy(out + pos, tmp, copy);
            pos += copy;
        }
    }
    out[pos] = '\0';
    return (int)pos;
}

/* =========================================================================
 * Thunk Registration Table
 * ========================================================================= */
static AsiThunkEntry s_thunks[ASI_THUNK_MAX];
static uint32_t      s_thunk_count = 0;

static int is_crt_dll(const char *dll) {
    if (!dll) return 0;
    return (strncasecmp(dll, "api-ms-win-crt", 14) == 0 ||
            strncasecmp(dll, "ucrtbase", 8) == 0 ||
            strncasecmp(dll, "msvcrt", 6) == 0 ||
            strncasecmp(dll, "vcruntime", 9) == 0 ||
            strncasecmp(dll, "msvcr", 5) == 0);
}

static int api_is_known(const char *dll, const char *name) {
    if (!dll || !name) return 0;
    static const char *const kKnown[] = {
        /* KERNEL32 */
        "VirtualProtect", "VirtualAlloc", "VirtualFree", "VirtualQuery", "FlushInstructionCache",
        "GetModuleHandleA", "GetModuleHandleW", "GetModuleHandleExA", "GetModuleHandleExW",
        "GetModuleFileNameA", "GetModuleFileNameW", "GetProcAddress", "LoadLibraryA", "LoadLibraryW",
        "LoadLibraryExA", "LoadLibraryExW", "FreeLibrary", "DisableThreadLibraryCalls",
        "GetPrivateProfileIntA", "GetPrivateProfileStringA", "WritePrivateProfileStringA",
        "CreateFileA", "CreateFileW", "ReadFile", "WriteFile", "SetFilePointer", "SetFilePointerEx",
        "GetFileSize", "GetFileSizeEx", "GetFileAttributesA", "GetFileAttributesW", "SetEndOfFile",
        "CreateDirectoryA", "CreateDirectoryW", "DeleteFileA", "DeleteFileW",
        "FindFirstFileA", "FindNextFileA", "FindClose", "CloseHandle", "DuplicateHandle",
        "GetCurrentDirectoryA", "GetCurrentDirectoryW", "GetFullPathNameA", "GetFullPathNameW", "GetTempPathA",
        "GetDriveTypeW", "GetFileInformationByHandle", "PeekNamedPipe", "FlushFileBuffers",
        "FindFirstFileExW", "FindNextFileW",
        "GetProcessHeap", "HeapAlloc", "HeapReAlloc", "HeapFree", "HeapSize", "LocalAlloc", "LocalFree",
        "InitializeCriticalSection", "InitializeCriticalSectionEx", "InitializeCriticalSectionAndSpinCount",
        "EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection",
        "InitializeSListHead", "InterlockedFlushSList",
        "TlsAlloc", "TlsGetValue", "TlsSetValue", "TlsFree",
        "FlsAlloc", "FlsGetValue", "FlsSetValue", "FlsFree",
        "GetTickCount", "GetTickCount64", "QueryPerformanceCounter", "QueryPerformanceFrequency",
        "GetSystemTimeAsFileTime", "GetLocalTime", "SystemTimeToTzSpecificLocalTime",
        "FileTimeToSystemTime", "GetTimeZoneInformation", "Sleep",
        "GetCurrentProcess", "GetCurrentProcessId", "GetCurrentThread", "GetCurrentThreadId",
        "GetLastError", "SetLastError", "GetCommandLineA", "GetCommandLineW",
        "GetEnvironmentVariableA", "GetEnvironmentVariableW", "SetEnvironmentVariableA", "SetEnvironmentVariableW",
        "GetEnvironmentStringsW", "FreeEnvironmentStringsW", "GetOEMCP", "GetACP", "GetCPInfo", "IsValidCodePage",
        "MultiByteToWideChar", "WideCharToMultiByte", "LCMapStringW", "LCMapStringEx", "GetStringTypeW",
        "CompareStringW", "GetLocaleInfoW", "IsValidLocale", "GetUserDefaultLCID", "EnumSystemLocalesW",
        "GetStdHandle", "SetStdHandle", "GetFileType", "GetConsoleOutputCP", "GetConsoleMode",
        "WriteConsoleA", "WriteConsoleW", "ReadConsoleW", "OutputDebugStringA",
        "CreateEventA", "CreateEventW", "CreateMutexA", "CreateSemaphoreA",
        "WaitForSingleObject", "WaitForSingleObjectEx", "ReleaseMutex",
        "ReleaseSemaphore", "SetEvent", "ResetEvent", "CreateThread", "ExitThread", "ExitProcess", "TerminateProcess",
        "IsDebuggerPresent", "IsProcessorFeaturePresent", "SetUnhandledExceptionFilter",
        "UnhandledExceptionFilter", "AddVectoredExceptionHandler", "RtlUnwind", "RaiseException",
        "EncodePointer", "DecodePointer", "FormatMessageA", "GetStartupInfoW", "GetVersionExA",
        /* USER32 */
        "GetAsyncKeyState", "MessageBoxA", "MessageBoxW", "GetDesktopWindow", "GetClientRect",
        "GetWindowRect", "GetWindowLongA", "SetWindowLongA", "SetWindowPos", "EnumWindows",
        "GetWindow", "GetWindowThreadProcessId", "IsWindowVisible", "FindWindowA",
        /* SHELL32 */
        "SHGetFolderPathA",
        /* ADVAPI32 */
        "CryptAcquireContextA", "CryptCreateHash", "CryptHashData", "CryptGetHashParam",
        "CryptDestroyHash", "CryptReleaseContext",
        /* CRT / UCRT */
        "malloc", "calloc", "realloc", "free",
        "memcpy", "memmove", "memset", "memcmp", "memchr",
        "strlen", "strnlen", "wcslen", "wcsnlen", "strcmp", "_stricmp", "strncmp", "_strnicmp",
        "strcpy", "strncpy", "strcat", "strncat", "strchr", "strrchr", "strstr", "strcspn", "_strdup",
        "tolower", "toupper", "isdigit", "isspace", "isxdigit", "isalpha", "isalnum",
        "strtol", "strtoul", "strtoll", "strtoull", "atoi", "atof", "mbrlen", "mbrtowc", "mbsrtowcs", "wcrtomb",
        "sqrt", "tanh", "lroundf", "__setusermatherr", "rand_s", "localeconv", "getenv", "strerror",
        "fopen", "fclose", "fread", "fwrite", "fgets", "fputs", "fputc", "fflush", "getc", "ungetc",
        "fseek", "ftell", "_fileno", "_read", "_setmode", "_lock_file", "_unlock_file",
        "__acrt_iob_func", "__stdio_common_vsprintf", "__stdio_common_vfprintf",
        "sprintf", "snprintf", "_snprintf", "vsnprintf", "_vsnprintf", "printf", "fprintf",
        "_initialize_onexit_table", "_register_onexit_function", "_execute_onexit_table",
        "_initterm", "_initterm_e", "_errno", "_beginthreadex", "_endthreadex",
        "_assert", "abort", "_exit", "exit", "_setjmp3", "longjmp",
        "__CxxFrameHandler3", "_CxxThrowException",
        /* WS2_32 */
        "WSAStartup", "WSAGetLastError", "socket", "closesocket", "bind", "setsockopt",
        "ioctlsocket", "WSAIoctl", "sendto", "recvfrom", "select",
        NULL
    };
    for (int i = 0; kKnown[i]; i++) {
        if (strcmp(name, kKnown[i]) == 0) return 1;
    }
    return 0;
}

void asi_win32_init(const AsiWin32Host *host) {
    memset(&s_host, 0, sizeof(s_host));
    if (host) s_host = *host;
    s_mem = s_host.mem_base;

    memset(s_win_files, 0, sizeof(s_win_files));
    memset(s_find_handles, 0, sizeof(s_find_handles));
    memset(s_crt_files, 0, sizeof(s_crt_files));
    memset(s_tls_values, 0, sizeof(s_tls_values));
    memset(s_tls_used, 0, sizeof(s_tls_used));
    memset(s_thunks, 0, sizeof(s_thunks));
    s_thunk_count = 0;
    s_heap_block_count = 0;
    s_heap_cursor = ASI_HEAP_ARENA;
    s_last_error = 0;

    /* Pre-allocate standard CRT/Win32 singleton buffers in guest heap */
    s_crt_errno_va = asi_heap_alloc(16, 1);
    s_crt_iob_va   = asi_heap_alloc(192, 1); /* 3 dummy FILE structs (stdin/stdout/stderr) */
    s_cmdline_a_va = asi_heap_alloc(256, 1);
    s_cmdline_w_va = asi_heap_alloc(512, 1);
    s_locale_va    = asi_heap_alloc(128, 1);

    if (s_cmdline_a_va && s_mem) {
        snprintf((char *)(s_mem + s_cmdline_a_va), 256, "\"C:\\nfsu2\\SPEED2.EXE\"");
    }
    if (s_cmdline_w_va && s_mem) {
        ascii_to_utf16("\"C:\\nfsu2\\SPEED2.EXE\"", s_cmdline_w_va, 250);
    }
    if (s_locale_va && s_mem) {
        uint32_t dot = asi_heap_alloc(4, 1);
        uint32_t empty = asi_heap_alloc(4, 1);
        if (dot) { s_mem[dot] = '.'; s_mem[dot + 1] = '\0'; }
        /* struct lconv: decimal_point = ".", thousands_sep = "", grouping = "" */
        g_wr32(s_locale_va + 0, dot);
        g_wr32(s_locale_va + 4, empty);
        g_wr32(s_locale_va + 8, empty);
    }

    /* Setup standard CRT FILE* slots 0,1,2 -> stdin, stdout, stderr */
    s_crt_files[0].in_use = 1; s_crt_files[0].fp = stdin;  s_crt_files[0].guest_struct_va = s_crt_iob_va + 0;
    s_crt_files[1].in_use = 1; s_crt_files[1].fp = stdout; s_crt_files[1].guest_struct_va = s_crt_iob_va + 64;
    s_crt_files[2].in_use = 1; s_crt_files[2].fp = stderr; s_crt_files[2].guest_struct_va = s_crt_iob_va + 128;
}

uint32_t asi_win32_register_thunk(const char *dll, const char *name, int *out_resolved) {
    for (uint32_t i = 0; i < s_thunk_count; i++) {
        if (strcasecmp(s_thunks[i].dll, dll) == 0 && strcmp(s_thunks[i].name, name) == 0) {
            if (out_resolved) *out_resolved = s_thunks[i].resolved;
            return s_thunks[i].thunk_va;
        }
    }
    if (s_thunk_count >= ASI_THUNK_MAX) {
        asi_log_err("ASI thunk table full (%u)", ASI_THUNK_MAX);
        if (out_resolved) *out_resolved = 0;
        return 0;
    }
    AsiThunkEntry *e = &s_thunks[s_thunk_count];
    strncpy(e->dll, dll ? dll : "unknown", sizeof(e->dll) - 1);
    strncpy(e->name, name ? name : "unknown", sizeof(e->name) - 1);
    e->thunk_va = ASI_THUNK_BASE + s_thunk_count * ASI_THUNK_STRIDE;
    e->resolved = api_is_known(e->dll, e->name);
    e->logged_unimpl = 0;
    s_thunk_count++;
    if (out_resolved) *out_resolved = e->resolved;
    return e->thunk_va;
}

static FILE *crt_lookup_fp(uint32_t guest_fp) {
    for (int i = 0; i < MAX_CRT_FILES; i++) {
        if (s_crt_files[i].in_use && s_crt_files[i].guest_struct_va == guest_fp) {
            return s_crt_files[i].fp;
        }
    }
    return NULL;
}

/* =========================================================================
 * Main Win32 / UCRT Thunk Dispatcher
 * ========================================================================= */
uint32_t asi_win32_dispatch(uint32_t thunk_va, AsiCallRegs *r) {
    uint32_t ret_va = g_rd32(r->esp);
    uint32_t idx = (thunk_va - ASI_THUNK_BASE) / ASI_THUNK_STRIDE;
    if (idx >= s_thunk_count) {
        asi_log_err("Invalid thunk VA 0x%08X called from 0x%08X", thunk_va, ret_va);
        r->eax = 0;
        r->esp += 4u;
        return ret_va;
    }

    AsiThunkEntry *e = &s_thunks[idx];
    const char *fn = e->name;
    asi_log_trace_api(e->dll, fn, ret_va);

    /* Helper macros for calling convention return:
     * RET_STD(nargs, val): stdcall cleans return address + nargs*4
     * RET_CDECL(val):      cdecl cleans only return address (caller cleans args) */
#define RET_STD(nargs, val)   do { r->eax = (uint32_t)(val); r->esp += 4u + (uint32_t)(nargs) * 4u; return ret_va; } while (0)
#define RET_CDECL(val)        do { r->eax = (uint32_t)(val); r->esp += 4u; return ret_va; } while (0)

    /* --- Memory Protection & Allocation --- */
    if (strcmp(fn, "VirtualProtect") == 0) {
        uint32_t va = g_arg(r, 0), sz = g_arg(r, 1), fl_new = g_arg(r, 2), p_old = g_arg(r, 3);
        if (p_old) g_wr32(p_old, 0x40u); /* PAGE_EXECUTE_READWRITE */
        if (s_host.notify_virtual_protect) s_host.notify_virtual_protect(va, sz, fl_new);
        RET_STD(4, 1);
    }
    if (strcmp(fn, "VirtualAlloc") == 0) {
        uint32_t sz = g_arg(r, 1);
        uint32_t va = asi_heap_alloc(sz ? sz : 4096u, 1);
        RET_STD(4, va);
    }
    if (strcmp(fn, "VirtualFree") == 0) {
        asi_heap_free(g_arg(r, 0));
        RET_STD(3, 1);
    }
    if (strcmp(fn, "VirtualQuery") == 0) {
        uint32_t va = g_arg(r, 0), buf = g_arg(r, 1), len = g_arg(r, 2);
        if (buf && len >= 28u) {
            memset(g_ptr(buf), 0, 28);
            g_wr32(buf + 0, va & ~0xFFFu);
            g_wr32(buf + 4, va & ~0xFFFFu);
            g_wr32(buf + 8, 0x40u);
            g_wr32(buf + 12, 0x100000u);
            g_wr32(buf + 16, 0x1000u); /* MEM_COMMIT */
            g_wr32(buf + 20, 0x40u);   /* PAGE_EXECUTE_READWRITE */
            g_wr32(buf + 24, 0x1000000u);
            RET_STD(3, 28u);
        }
        RET_STD(3, 0);
    }
    if (strcmp(fn, "FlushInstructionCache") == 0) {
        RET_STD(3, 1);
    }
    if (strcmp(fn, "GetProcessHeap") == 0) {
        RET_STD(0, 0x0000BEEFu);
    }
    if (strcmp(fn, "HeapAlloc") == 0) {
        uint32_t flags = g_arg(r, 1), sz = g_arg(r, 2);
        RET_STD(3, asi_heap_alloc(sz, (flags & 0x08u) != 0));
    }
    if (strcmp(fn, "HeapReAlloc") == 0) {
        uint32_t ptr = g_arg(r, 2), sz = g_arg(r, 3);
        RET_STD(4, asi_heap_realloc(ptr, sz));
    }
    if (strcmp(fn, "HeapFree") == 0) {
        asi_heap_free(g_arg(r, 2));
        RET_STD(3, 1);
    }
    if (strcmp(fn, "HeapSize") == 0) {
        RET_STD(3, asi_heap_size(g_arg(r, 2)));
    }
    if (strcmp(fn, "LocalAlloc") == 0) {
        uint32_t flags = g_arg(r, 0), sz = g_arg(r, 1);
        RET_STD(2, asi_heap_alloc(sz, (flags & 0x40u) != 0));
    }
    if (strcmp(fn, "LocalFree") == 0) {
        asi_heap_free(g_arg(r, 0));
        RET_STD(1, 0);
    }

    /* --- Modules & Symbols --- */
    if (strcmp(fn, "GetModuleHandleA") == 0) {
        uint32_t name_va = g_arg(r, 0);
        const char *nm = name_va ? g_cstr(name_va) : NULL;
        uint32_t h = s_host.resolve_mod_handle ? s_host.resolve_mod_handle(nm) : 0x00400000u;
        RET_STD(1, h);
    }
    if (strcmp(fn, "GetModuleHandleW") == 0) {
        uint32_t name_va = g_arg(r, 0);
        char nm[256];
        if (name_va) utf16_to_ascii(name_va, nm, sizeof(nm));
        uint32_t h = s_host.resolve_mod_handle ? s_host.resolve_mod_handle(name_va ? nm : NULL) : 0x00400000u;
        RET_STD(1, h);
    }
    if (strcmp(fn, "GetModuleHandleExA") == 0 || strcmp(fn, "GetModuleHandleExW") == 0) {
        uint32_t flags = g_arg(r, 0), name_va = g_arg(r, 1), out_va = g_arg(r, 2);
        uint32_t h = 0x00400000u;
        if (flags & 0x04u) { /* GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS */
            const AsiLoadedModInfo *m = s_host.find_mod_by_addr ? s_host.find_mod_by_addr(name_va) : NULL;
            h = m ? m->base_va : 0x00400000u;
        } else if (name_va) {
            char nm[256];
            if (fn[17] == 'W') utf16_to_ascii(name_va, nm, sizeof(nm));
            else snprintf(nm, sizeof(nm), "%s", g_cstr(name_va) ? g_cstr(name_va) : "");
            h = s_host.resolve_mod_handle ? s_host.resolve_mod_handle(nm) : 0;
        }
        if (out_va) g_wr32(out_va, h);
        RET_STD(3, h != 0);
    }
    if (strcmp(fn, "GetModuleFileNameA") == 0) {
        uint32_t hmod = g_arg(r, 0), buf_va = g_arg(r, 1), sz = g_arg(r, 2);
        char path[512];
        const AsiLoadedModInfo *m = (hmod && s_host.find_mod_by_handle) ? s_host.find_mod_by_handle(hmod) : NULL;
        if (m) snprintf(path, sizeof(path), "C:\\nfsu2\\scripts\\%s", m->name);
        else   snprintf(path, sizeof(path), "C:\\nfsu2\\SPEED2.EXE");
        uint32_t n = 0;
        if (buf_va && sz > 0) {
            n = (uint32_t)snprintf((char *)g_ptr(buf_va), sz, "%s", path);
            if (n >= sz) n = sz - 1;
        }
        RET_STD(3, n);
    }
    if (strcmp(fn, "GetModuleFileNameW") == 0) {
        uint32_t hmod = g_arg(r, 0), buf_va = g_arg(r, 1), sz = g_arg(r, 2);
        char path[512];
        const AsiLoadedModInfo *m = (hmod && s_host.find_mod_by_handle) ? s_host.find_mod_by_handle(hmod) : NULL;
        if (m) snprintf(path, sizeof(path), "C:\\nfsu2\\scripts\\%s", m->name);
        else   snprintf(path, sizeof(path), "C:\\nfsu2\\SPEED2.EXE");
        uint32_t n = ascii_to_utf16(path, buf_va, sz);
        RET_STD(3, n);
    }
    if (strcmp(fn, "GetProcAddress") == 0) {
        uint32_t hmod = g_arg(r, 0), proc_va = g_arg(r, 1);
        const char *p = (proc_va > 0xFFFFu) ? g_cstr(proc_va) : NULL;
        uint32_t res = (s_host.resolve_proc_addr && p) ? s_host.resolve_proc_addr(hmod, p) : 0;
        RET_STD(2, res);
    }
    if (strcmp(fn, "LoadLibraryA") == 0 || strcmp(fn, "LoadLibraryW") == 0) {
        uint32_t nm_va = g_arg(r, 0);
        char nm[256] = {0};
        if (nm_va) {
            if (fn[11] == 'W') utf16_to_ascii(nm_va, nm, sizeof(nm));
            else snprintf(nm, sizeof(nm), "%s", g_cstr(nm_va) ? g_cstr(nm_va) : "");
        }
        uint32_t h = s_host.load_library ? s_host.load_library(nm) : 0;
        RET_STD(1, h);
    }
    if (strcmp(fn, "LoadLibraryExA") == 0 || strcmp(fn, "LoadLibraryExW") == 0) {
        uint32_t nm_va = g_arg(r, 0);
        char nm[256] = {0};
        if (nm_va) {
            if (fn[13] == 'W') utf16_to_ascii(nm_va, nm, sizeof(nm));
            else snprintf(nm, sizeof(nm), "%s", g_cstr(nm_va) ? g_cstr(nm_va) : "");
        }
        uint32_t h = s_host.load_library ? s_host.load_library(nm) : 0;
        RET_STD(3, h);
    }
    if (strcmp(fn, "FreeLibrary") == 0) { RET_STD(1, 1); }
    if (strcmp(fn, "DisableThreadLibraryCalls") == 0) { RET_STD(1, 1); }

    /* --- INI Profile Helpers --- */
    if (strcmp(fn, "GetPrivateProfileIntA") == 0) {
        const char *sec = g_cstr(g_arg(r, 0));
        const char *key = g_cstr(g_arg(r, 1));
        int defval = (int)g_arg(r, 2);
        const char *file = g_cstr(g_arg(r, 3));
        char host_p[1024], val[256];
        map_win_path(file, host_p, sizeof(host_p));
        int res = defval;
        if (ini_read_value(host_p, sec, key, val, sizeof(val))) {
            res = (int)strtol(val, NULL, 0);
        }
        RET_STD(4, (uint32_t)res);
    }
    if (strcmp(fn, "GetPrivateProfileStringA") == 0) {
        const char *sec = g_cstr(g_arg(r, 0));
        const char *key = g_cstr(g_arg(r, 1));
        const char *def = g_cstr(g_arg(r, 2));
        uint32_t out_va = g_arg(r, 3), out_sz = g_arg(r, 4);
        const char *file = g_cstr(g_arg(r, 5));
        char host_p[1024], val[512];
        map_win_path(file, host_p, sizeof(host_p));
        const char *use = def ? def : "";
        if (ini_read_value(host_p, sec, key, val, sizeof(val))) {
            use = val;
        }
        uint32_t n = 0;
        if (out_va && out_sz > 0) {
            n = (uint32_t)snprintf((char *)g_ptr(out_va), out_sz, "%s", use);
            if (n >= out_sz) n = out_sz - 1;
        }
        RET_STD(6, n);
    }
    if (strcmp(fn, "WritePrivateProfileStringA") == 0) {
        RET_STD(4, 1);
    }

    /* --- Win32 File I/O --- */
    if (strcmp(fn, "CreateFileA") == 0 || strcmp(fn, "CreateFileW") == 0) {
        char win_p[512] = {0}, host_p[1024], resolved[1024];
        if (fn[10] == 'W') utf16_to_ascii(g_arg(r, 0), win_p, sizeof(win_p));
        else snprintf(win_p, sizeof(win_p), "%s", g_cstr(g_arg(r, 0)) ? g_cstr(g_arg(r, 0)) : "");
        uint32_t access = g_arg(r, 1), disp = g_arg(r, 4);
        map_win_path(win_p, host_p, sizeof(host_p));
        resolve_case_path(host_p, resolved, sizeof(resolved));

        const char *mode = "rb";
        if (access & 0x40000000u) { /* GENERIC_WRITE */
            if (disp == 2u) mode = (access & 0x80000000u) ? "w+b" : "wb"; /* CREATE_ALWAYS */
            else if (disp == 4u) mode = "a+b"; /* OPEN_ALWAYS */
            else mode = "r+b";
        }
        FILE *fp = fopen(resolved, mode);
        if (!fp && (access & 0x40000000u) && disp != 3u) fp = fopen(resolved, "w+b");
        if (!fp) {
            s_last_error = 2u; /* ERROR_FILE_NOT_FOUND */
            RET_STD(7, 0xFFFFFFFFu);
        }
        for (int i = 0; i < MAX_WIN_FILES; i++) {
            if (!s_win_files[i].in_use) {
                s_win_files[i].in_use = 1;
                s_win_files[i].fp = fp;
                RET_STD(7, 0x1000u + (uint32_t)i);
            }
        }
        fclose(fp);
        RET_STD(7, 0xFFFFFFFFu);
    }
    if (strcmp(fn, "ReadFile") == 0) {
        uint32_t h = g_arg(r, 0), buf = g_arg(r, 1), n = g_arg(r, 2), out_n = g_arg(r, 3);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            size_t got = fread(g_ptr(buf), 1, n, s_win_files[h - 0x1000u].fp);
            if (out_n) g_wr32(out_n, (uint32_t)got);
            RET_STD(5, 1);
        }
        RET_STD(5, 0);
    }
    if (strcmp(fn, "WriteFile") == 0) {
        uint32_t h = g_arg(r, 0), buf = g_arg(r, 1), n = g_arg(r, 2), out_n = g_arg(r, 3);
        if (h == 0xFFFFFFF5u || h == 0xFFFFFFF4u) { /* stdout / stderr */
            if (out_n) g_wr32(out_n, n);
            RET_STD(5, 1);
        }
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            size_t put = fwrite(g_ptr(buf), 1, n, s_win_files[h - 0x1000u].fp);
            fflush(s_win_files[h - 0x1000u].fp);
            if (out_n) g_wr32(out_n, (uint32_t)put);
            RET_STD(5, 1);
        }
        RET_STD(5, 0);
    }
    if (strcmp(fn, "SetFilePointer") == 0) {
        uint32_t h = g_arg(r, 0), dist = g_arg(r, 1), method = g_arg(r, 3);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            int wh = (method == 1u) ? SEEK_CUR : (method == 2u ? SEEK_END : SEEK_SET);
            fseek(s_win_files[h - 0x1000u].fp, (long)(int32_t)dist, wh);
            RET_STD(4, (uint32_t)ftell(s_win_files[h - 0x1000u].fp));
        }
        RET_STD(4, 0xFFFFFFFFu);
    }
    if (strcmp(fn, "SetFilePointerEx") == 0) {
        uint32_t h = g_arg(r, 0), lo = g_arg(r, 1), out_va = g_arg(r, 3), method = g_arg(r, 4);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            int wh = (method == 1u) ? SEEK_CUR : (method == 2u ? SEEK_END : SEEK_SET);
            fseek(s_win_files[h - 0x1000u].fp, (long)(int32_t)lo, wh);
            long pos = ftell(s_win_files[h - 0x1000u].fp);
            if (out_va) { g_wr32(out_va, (uint32_t)pos); g_wr32(out_va + 4, 0); }
            RET_STD(5, 1);
        }
        RET_STD(5, 0);
    }
    if (strcmp(fn, "GetFileSize") == 0) {
        uint32_t h = g_arg(r, 0), hi_va = g_arg(r, 1);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            FILE *fp = s_win_files[h - 0x1000u].fp;
            long cur = ftell(fp);
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, cur, SEEK_SET);
            if (hi_va) g_wr32(hi_va, 0);
            RET_STD(2, (uint32_t)sz);
        }
        RET_STD(2, 0xFFFFFFFFu);
    }
    if (strcmp(fn, "GetFileSizeEx") == 0) {
        uint32_t h = g_arg(r, 0), out_va = g_arg(r, 1);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            FILE *fp = s_win_files[h - 0x1000u].fp;
            long cur = ftell(fp);
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, cur, SEEK_SET);
            if (out_va) { g_wr32(out_va, (uint32_t)sz); g_wr32(out_va + 4, 0); }
            RET_STD(2, 1);
        }
        RET_STD(2, 0);
    }
    if (strcmp(fn, "SetEndOfFile") == 0) { RET_STD(1, 1); }
    if (strcmp(fn, "GetFileAttributesA") == 0 || strcmp(fn, "GetFileAttributesW") == 0) {
        char win_p[512] = {0}, host_p[1024], resolved[1024];
        if (fn[17] == 'W') utf16_to_ascii(g_arg(r, 0), win_p, sizeof(win_p));
        else snprintf(win_p, sizeof(win_p), "%s", g_cstr(g_arg(r, 0)) ? g_cstr(g_arg(r, 0)) : "");
        map_win_path(win_p, host_p, sizeof(host_p));
        resolve_case_path(host_p, resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) != 0) RET_STD(1, 0xFFFFFFFFu);
        RET_STD(1, S_ISDIR(st.st_mode) ? 0x10u : 0x80u);
    }
    if (strcmp(fn, "CreateDirectoryA") == 0 || strcmp(fn, "CreateDirectoryW") == 0) {
        char win_p[512] = {0}, host_p[1024];
        if (fn[15] == 'W') utf16_to_ascii(g_arg(r, 0), win_p, sizeof(win_p));
        else snprintf(win_p, sizeof(win_p), "%s", g_cstr(g_arg(r, 0)) ? g_cstr(g_arg(r, 0)) : "");
        map_win_path(win_p, host_p, sizeof(host_p));
        int ok = (mkdir(host_p, 0775) == 0 || errno == EEXIST);
        RET_STD(2, ok);
    }
    if (strcmp(fn, "DeleteFileA") == 0 || strcmp(fn, "DeleteFileW") == 0) {
        char win_p[512] = {0}, host_p[1024], resolved[1024];
        if (fn[10] == 'W') utf16_to_ascii(g_arg(r, 0), win_p, sizeof(win_p));
        else snprintf(win_p, sizeof(win_p), "%s", g_cstr(g_arg(r, 0)) ? g_cstr(g_arg(r, 0)) : "");
        map_win_path(win_p, host_p, sizeof(host_p));
        resolve_case_path(host_p, resolved, sizeof(resolved));
        RET_STD(1, unlink(resolved) == 0);
    }
    if (strcmp(fn, "FindFirstFileA") == 0) {
        const char *pat_in = g_cstr(g_arg(r, 0));
        uint32_t data_va = g_arg(r, 1);
        char host_p[1024];
        map_win_path(pat_in, host_p, sizeof(host_p));
        char dir[1024] = {0}, pat[256] = "*";
        const char *sl = strrchr(host_p, '/');
        if (sl) {
            size_t dl = (size_t)(sl - host_p);
            if (dl >= sizeof(dir)) dl = sizeof(dir) - 1;
            memcpy(dir, host_p, dl);
            dir[dl] = '\0';
            snprintf(pat, sizeof(pat), "%s", sl + 1);
        } else {
            snprintf(dir, sizeof(dir), "%s", s_host.game_dir);
            snprintf(pat, sizeof(pat), "%s", host_p);
        }
        DIR *dp = opendir(dir);
        if (!dp) RET_STD(2, 0xFFFFFFFFu);
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if (!wild_match(pat, de->d_name)) continue;
            for (int i = 0; i < MAX_FIND_HND; i++) {
                if (!s_find_handles[i].in_use) {
                    s_find_handles[i].in_use = 1;
                    s_find_handles[i].dp = dp;
                    snprintf(s_find_handles[i].dir_path, sizeof(s_find_handles[i].dir_path), "%s", dir);
                    snprintf(s_find_handles[i].pattern, sizeof(s_find_handles[i].pattern), "%s", pat);
                    if (data_va) {
                        memset(g_ptr(data_va), 0, 320);
                        char full[1280];
                        snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
                        struct stat st;
                        uint32_t attr = 0x80u, fsz = 0;
                        if (stat(full, &st) == 0) {
                            if (S_ISDIR(st.st_mode)) attr = 0x10u;
                            fsz = (uint32_t)st.st_size;
                        }
                        g_wr32(data_va + 0, attr);
                        g_wr32(data_va + 32, fsz);
                        snprintf((char *)g_ptr(data_va + 44), 260, "%s", de->d_name);
                    }
                    RET_STD(2, 0x2000u + (uint32_t)i);
                }
            }
            break;
        }
        closedir(dp);
        RET_STD(2, 0xFFFFFFFFu);
    }
    if (strcmp(fn, "FindNextFileA") == 0) {
        uint32_t h = g_arg(r, 0), data_va = g_arg(r, 1);
        if (h >= 0x2000u && h < 0x2000u + MAX_FIND_HND && s_find_handles[h - 0x2000u].in_use) {
            WinFindHandle *fh = &s_find_handles[h - 0x2000u];
            struct dirent *de;
            while ((de = readdir(fh->dp)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                if (!wild_match(fh->pattern, de->d_name)) continue;
                if (data_va) {
                    memset(g_ptr(data_va), 0, 320);
                    char full[1280];
                    snprintf(full, sizeof(full), "%s/%s", fh->dir_path, de->d_name);
                    struct stat st;
                    uint32_t attr = 0x80u, fsz = 0;
                    if (stat(full, &st) == 0) {
                        if (S_ISDIR(st.st_mode)) attr = 0x10u;
                        fsz = (uint32_t)st.st_size;
                    }
                    g_wr32(data_va + 0, attr);
                    g_wr32(data_va + 32, fsz);
                    snprintf((char *)g_ptr(data_va + 44), 260, "%s", de->d_name);
                }
                RET_STD(2, 1);
            }
        }
        s_last_error = 18u; /* ERROR_NO_MORE_FILES */
        RET_STD(2, 0);
    }
    if (strcmp(fn, "FindClose") == 0) {
        uint32_t h = g_arg(r, 0);
        if (h >= 0x2000u && h < 0x2000u + MAX_FIND_HND && s_find_handles[h - 0x2000u].in_use) {
            closedir(s_find_handles[h - 0x2000u].dp);
            s_find_handles[h - 0x2000u].in_use = 0;
        }
        RET_STD(1, 1);
    }
    if (strcmp(fn, "CloseHandle") == 0) {
        uint32_t h = g_arg(r, 0);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            fclose(s_win_files[h - 0x1000u].fp);
            s_win_files[h - 0x1000u].in_use = 0;
        }
        RET_STD(1, 1);
    }
    if (strcmp(fn, "DuplicateHandle") == 0) {
        uint32_t src = g_arg(r, 1), out_va = g_arg(r, 3);
        if (out_va) g_wr32(out_va, src);
        RET_STD(7, 1);
    }
    if (strcmp(fn, "GetCurrentDirectoryA") == 0) {
        uint32_t sz = g_arg(r, 0), buf = g_arg(r, 1);
        uint32_t n = (buf && sz) ? (uint32_t)snprintf((char *)g_ptr(buf), sz, "C:\\nfsu2") : 8u;
        RET_STD(2, n);
    }
    if (strcmp(fn, "GetCurrentDirectoryW") == 0) {
        uint32_t sz = g_arg(r, 0), buf = g_arg(r, 1);
        uint32_t n = (buf && sz) ? ascii_to_utf16("C:\\nfsu2", buf, sz) : 8u;
        RET_STD(2, n);
    }
    if (strcmp(fn, "GetFullPathNameA") == 0) {
        const char *in = g_cstr(g_arg(r, 0));
        uint32_t sz = g_arg(r, 1), buf = g_arg(r, 2), part = g_arg(r, 3);
        char full[512];
        if (in && in[0] && in[1] == ':') snprintf(full, sizeof(full), "%s", in);
        else snprintf(full, sizeof(full), "C:\\nfsu2\\%s", in ? in : "");
        uint32_t n = (buf && sz) ? (uint32_t)snprintf((char *)g_ptr(buf), sz, "%s", full) : (uint32_t)strlen(full) + 1u;
        if (part && buf) {
            const char *sl = strrchr((const char *)g_ptr(buf), '\\');
            g_wr32(part, sl ? (buf + (uint32_t)(sl + 1 - (const char *)g_ptr(buf))) : buf);
        }
        RET_STD(4, n);
    }
    if (strcmp(fn, "GetFullPathNameW") == 0) {
        char in[512], full[512];
        utf16_to_ascii(g_arg(r, 0), in, sizeof(in));
        uint32_t sz = g_arg(r, 1), buf = g_arg(r, 2), part = g_arg(r, 3);
        if (in[0] && in[1] == ':') snprintf(full, sizeof(full), "%s", in);
        else snprintf(full, sizeof(full), "C:\\nfsu2\\%s", in);
        uint32_t n = (buf && sz) ? ascii_to_utf16(full, buf, sz) : (uint32_t)strlen(full) + 1u;
        if (part) g_wr32(part, 0);
        RET_STD(4, n);
    }
    if (strcmp(fn, "GetDriveTypeW") == 0) { RET_STD(1, 3u); /* DRIVE_FIXED */ }
    if (strcmp(fn, "GetFileInformationByHandle") == 0) {
        uint32_t h = g_arg(r, 0), info = g_arg(r, 1);
        if (info) {
            memset(g_ptr(info), 0, 52);
            g_wr32(info + 0, 0x80u); /* FILE_ATTRIBUTE_NORMAL */
            if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
                FILE *fp = s_win_files[h - 0x1000u].fp;
                long cur = ftell(fp);
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, cur, SEEK_SET);
                g_wr32(info + 36, (uint32_t)sz);
            }
        }
        RET_STD(2, 1);
    }
    if (strcmp(fn, "FlushFileBuffers") == 0) {
        uint32_t h = g_arg(r, 0);
        if (h >= 0x1000u && h < 0x1000u + MAX_WIN_FILES && s_win_files[h - 0x1000u].in_use) {
            fflush(s_win_files[h - 0x1000u].fp);
        }
        RET_STD(1, 1);
    }
    if (strcmp(fn, "PeekNamedPipe") == 0) { RET_STD(6, 0); }
    if (strcmp(fn, "FindFirstFileExW") == 0) { RET_STD(6, 0xFFFFFFFFu); }
    if (strcmp(fn, "FindNextFileW") == 0)    { s_last_error = 18u; RET_STD(2, 0); }
    if (strcmp(fn, "SHGetFolderPathA") == 0) {
        uint32_t out_va = g_arg(r, 4);
        if (out_va) snprintf((char *)g_ptr(out_va), 260, "C:\\nfsu2\\AppData");
        RET_STD(5, 0);
    }

    /* --- Sync / TLS / Process / Time --- */
    if (strcmp(fn, "InitializeSListHead") == 0) {
        uint32_t p = g_arg(r, 0);
        if (p) { g_wr32(p, 0); g_wr32(p + 4, 0); }
        RET_STD(1, 0);
    }
    if (strcmp(fn, "InterlockedFlushSList") == 0) {
        uint32_t p = g_arg(r, 0);
        uint32_t head = p ? g_rd32(p) : 0u;
        if (p) { g_wr32(p, 0); g_wr32(p + 4, 0); }
        RET_STD(1, head);
    }
    if (strcmp(fn, "InitializeCriticalSection") == 0) { RET_STD(1, 0); }
    if (strcmp(fn, "InitializeCriticalSectionAndSpinCount") == 0) { RET_STD(2, 1); }
    if (strcmp(fn, "InitializeCriticalSectionEx") == 0) { RET_STD(3, 1); }
    if (strcmp(fn, "EnterCriticalSection") == 0 ||
        strcmp(fn, "LeaveCriticalSection") == 0 ||
        strcmp(fn, "DeleteCriticalSection") == 0) { RET_STD(1, 0); }
    if (strcmp(fn, "TlsAlloc") == 0) {
        for (uint32_t i = 0; i < MAX_TLS_SLOTS; i++) {
            if (!s_tls_used[i]) { s_tls_used[i] = 1; s_tls_values[i] = 0; RET_STD(0, i); }
        }
        RET_STD(0, 0xFFFFFFFFu);
    }
    if (strcmp(fn, "FlsAlloc") == 0) {
        for (uint32_t i = 0; i < MAX_TLS_SLOTS; i++) {
            if (!s_tls_used[i]) { s_tls_used[i] = 1; s_tls_values[i] = 0; RET_STD(1, i); }
        }
        RET_STD(1, 0xFFFFFFFFu);
    }
    if (strcmp(fn, "TlsGetValue") == 0 || strcmp(fn, "FlsGetValue") == 0) {
        uint32_t slot = g_arg(r, 0);
        RET_STD(1, (slot < MAX_TLS_SLOTS) ? s_tls_values[slot] : 0u);
    }
    if (strcmp(fn, "TlsSetValue") == 0 || strcmp(fn, "FlsSetValue") == 0) {
        uint32_t slot = g_arg(r, 0), val = g_arg(r, 1);
        if (slot < MAX_TLS_SLOTS) s_tls_values[slot] = val;
        RET_STD(2, 1);
    }
    if (strcmp(fn, "TlsFree") == 0 || strcmp(fn, "FlsFree") == 0) {
        uint32_t slot = g_arg(r, 0);
        if (slot < MAX_TLS_SLOTS) s_tls_used[slot] = 0;
        RET_STD(1, 1);
    }
    if (strcmp(fn, "GetTickCount") == 0 || strcmp(fn, "GetTickCount64") == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
        if (strcmp(fn, "GetTickCount64") == 0) r->edx = (uint32_t)(ms >> 32);
        RET_STD(0, (uint32_t)ms);
    }
    if (strcmp(fn, "QueryPerformanceCounter") == 0) {
        uint32_t out_va = g_arg(r, 0);
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
        if (out_va) { g_wr32(out_va, (uint32_t)ns); g_wr32(out_va + 4, (uint32_t)(ns >> 32)); }
        RET_STD(1, 1);
    }
    if (strcmp(fn, "QueryPerformanceFrequency") == 0) {
        uint32_t out_va = g_arg(r, 0);
        uint64_t freq = 1000000000ULL;
        if (out_va) { g_wr32(out_va, (uint32_t)freq); g_wr32(out_va + 4, (uint32_t)(freq >> 32)); }
        RET_STD(1, 1);
    }
    if (strcmp(fn, "GetSystemTimeAsFileTime") == 0) {
        uint32_t out_va = g_arg(r, 0);
        uint64_t ft = ((uint64_t)time(NULL) + 11644473600ULL) * 10000000ULL;
        if (out_va) { g_wr32(out_va, (uint32_t)ft); g_wr32(out_va + 4, (uint32_t)(ft >> 32)); }
        RET_STD(1, 0);
    }
    if (strcmp(fn, "GetLocalTime") == 0 || strcmp(fn, "FileTimeToSystemTime") == 0 ||
        strcmp(fn, "SystemTimeToTzSpecificLocalTime") == 0) {
        int out_idx = (strcmp(fn, "GetLocalTime") == 0) ? 0 : (strcmp(fn, "FileTimeToSystemTime") == 0 ? 1 : 2);
        int nargs   = (strcmp(fn, "GetLocalTime") == 0) ? 1 : (strcmp(fn, "FileTimeToSystemTime") == 0 ? 2 : 3);
        uint32_t out_va = g_arg(r, out_idx);
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (out_va) {
            g_wr16(out_va + 0,  (uint16_t)(t.tm_year + 1900));
            g_wr16(out_va + 2,  (uint16_t)(t.tm_mon + 1));
            g_wr16(out_va + 4,  (uint16_t)t.tm_wday);
            g_wr16(out_va + 6,  (uint16_t)t.tm_mday);
            g_wr16(out_va + 8,  (uint16_t)t.tm_hour);
            g_wr16(out_va + 10, (uint16_t)t.tm_min);
            g_wr16(out_va + 12, (uint16_t)t.tm_sec);
            g_wr16(out_va + 14, 0);
        }
        RET_STD(nargs, nargs == 1 ? 0u : 1u);
    }
    if (strcmp(fn, "GetTimeZoneInformation") == 0) {
        uint32_t p = g_arg(r, 0);
        if (p) memset(g_ptr(p), 0, 172);
        RET_STD(1, 0);
    }
    if (strcmp(fn, "Sleep") == 0) {
        uint32_t ms = g_arg(r, 0);
        if (ms > 0 && ms < 200u) usleep(ms * 1000u);
        RET_STD(1, 0);
    }
    if (strcmp(fn, "GetCurrentProcess") == 0) { RET_STD(0, 0xFFFFFFFFu); }
    if (strcmp(fn, "GetCurrentThread") == 0)  { RET_STD(0, 0xFFFFFFFEu); }
    if (strcmp(fn, "GetCurrentProcessId") == 0) { RET_STD(0, (uint32_t)getpid()); }
    if (strcmp(fn, "GetCurrentThreadId") == 0)  { RET_STD(0, (uint32_t)(uintptr_t)pthread_self()); }
    if (strcmp(fn, "GetLastError") == 0) { RET_STD(0, s_last_error); }
    if (strcmp(fn, "SetLastError") == 0) { s_last_error = g_arg(r, 0); RET_STD(1, 0); }
    if (strcmp(fn, "GetCommandLineA") == 0) { RET_STD(0, s_cmdline_a_va); }
    if (strcmp(fn, "GetCommandLineW") == 0) { RET_STD(0, s_cmdline_w_va); }
    if (strcmp(fn, "GetEnvironmentVariableA") == 0) {
        const char *k = g_cstr(g_arg(r, 0));
        uint32_t buf = g_arg(r, 1), sz = g_arg(r, 2);
        const char *v = k ? getenv(k) : NULL;
        if (!v) { s_last_error = 203u; RET_STD(3, 0); }
        uint32_t n = (buf && sz) ? (uint32_t)snprintf((char *)g_ptr(buf), sz, "%s", v) : (uint32_t)strlen(v) + 1u;
        RET_STD(3, n);
    }
    if (strcmp(fn, "GetEnvironmentVariableW") == 0) {
        char k[128];
        utf16_to_ascii(g_arg(r, 0), k, sizeof(k));
        const char *v = getenv(k);
        if (!v) { s_last_error = 203u; RET_STD(3, 0); }
        RET_STD(3, ascii_to_utf16(v, g_arg(r, 1), g_arg(r, 2)));
    }
    if (strcmp(fn, "SetEnvironmentVariableA") == 0 || strcmp(fn, "SetEnvironmentVariableW") == 0) { RET_STD(2, 1); }
    if (strcmp(fn, "GetEnvironmentStringsW") == 0) {
        uint32_t va = asi_heap_alloc(16, 1);
        RET_STD(0, va);
    }
    if (strcmp(fn, "FreeEnvironmentStringsW") == 0) { asi_heap_free(g_arg(r, 0)); RET_STD(1, 1); }
    if (strcmp(fn, "GetOEMCP") == 0 || strcmp(fn, "GetACP") == 0 || strcmp(fn, "GetConsoleOutputCP") == 0) { RET_STD(0, 1252u); }
    if (strcmp(fn, "IsValidCodePage") == 0) { RET_STD(1, 1); }
    if (strcmp(fn, "IsValidLocale") == 0)   { RET_STD(2, 1); }
    if (strcmp(fn, "GetUserDefaultLCID") == 0) { RET_STD(0, 0x0409u); }
    if (strcmp(fn, "GetLocaleInfoW") == 0)  { RET_STD(4, 0); }
    if (strcmp(fn, "EnumSystemLocalesW") == 0) { RET_STD(2, 1); }
    if (strcmp(fn, "CompareStringW") == 0) {
        char a[256], b[256];
        utf16_to_ascii(g_arg(r, 2), a, sizeof(a));
        utf16_to_ascii(g_arg(r, 4), b, sizeof(b));
        int cmp = strcasecmp(a, b);
        RET_STD(6, cmp < 0 ? 1u : (cmp > 0 ? 3u : 2u)); /* CSTR_LESS_THAN=1, EQUAL=2, GREATER=3 */
    }
    if (strcmp(fn, "LCMapStringW") == 0)  { RET_STD(6, 0); }
    if (strcmp(fn, "LCMapStringEx") == 0) { RET_STD(9, 0); }
    if (strcmp(fn, "GetStringTypeW") == 0){ RET_STD(4, 1); }
    if (strcmp(fn, "GetCPInfo") == 0) {
        uint32_t out = g_arg(r, 1);
        if (out) { memset(g_ptr(out), 0, 20); g_wr32(out, 1); }
        RET_STD(2, 1);
    }
    if (strcmp(fn, "MultiByteToWideChar") == 0) {
        uint32_t src_va = g_arg(r, 2), src_len = g_arg(r, 3), dst_va = g_arg(r, 4), dst_len = g_arg(r, 5);
        const char *src = g_cstr(src_va);
        if (!src) RET_STD(6, 0);
        uint32_t slen = ((int32_t)src_len < 0) ? (uint32_t)strlen(src) + 1u : src_len;
        if (!dst_va || dst_len == 0) RET_STD(6, slen);
        uint32_t copy = slen < dst_len ? slen : dst_len;
        for (uint32_t i = 0; i < copy; i++) g_wr16(dst_va + i * 2u, (uint8_t)src[i]);
        RET_STD(6, copy);
    }
    if (strcmp(fn, "WideCharToMultiByte") == 0) {
        uint32_t src_va = g_arg(r, 2), src_len = g_arg(r, 3), dst_va = g_arg(r, 4), dst_len = g_arg(r, 5);
        uint32_t slen = 0;
        if ((int32_t)src_len < 0) {
            while (g_rd16(src_va + slen * 2u) != 0) slen++;
            slen++;
        } else {
            slen = src_len;
        }
        if (!dst_va || dst_len == 0) RET_STD(8, slen);
        uint32_t copy = slen < dst_len ? slen : dst_len;
        uint8_t *dst = g_ptr(dst_va);
        for (uint32_t i = 0; i < copy; i++) {
            uint16_t ch = g_rd16(src_va + i * 2u);
            dst[i] = (ch < 128u) ? (uint8_t)ch : '?';
        }
        RET_STD(8, copy);
    }
    if (strcmp(fn, "GetStdHandle") == 0) { RET_STD(1, g_arg(r, 0)); }
    if (strcmp(fn, "SetStdHandle") == 0) { RET_STD(2, 1); }
    if (strcmp(fn, "GetFileType") == 0)  { RET_STD(1, 2u); /* FILE_TYPE_CHAR */ }
    if (strcmp(fn, "GetConsoleMode") == 0) {
        uint32_t out = g_arg(r, 1);
        if (out) g_wr32(out, 0);
        RET_STD(2, 0);
    }
    if (strcmp(fn, "ReadConsoleW") == 0) { RET_STD(5, 0); }
    if (strcmp(fn, "WriteConsoleA") == 0 || strcmp(fn, "WriteConsoleW") == 0) {
        uint32_t n = g_arg(r, 2), out = g_arg(r, 3);
        if (out) g_wr32(out, n);
        RET_STD(5, 1);
    }
    if (strcmp(fn, "OutputDebugStringA") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        if (s) asi_log_info("[OutputDebugString] %s", s);
        RET_STD(1, 0);
    }
    if (strcmp(fn, "CreateEventA") == 0 || strcmp(fn, "CreateEventW") == 0 ||
        strcmp(fn, "CreateMutexA") == 0 || strcmp(fn, "CreateSemaphoreA") == 0) {
        int nargs = (strcmp(fn, "CreateMutexA") == 0) ? 3 : 4;
        RET_STD(nargs, 0x3001u);
    }
    if (strcmp(fn, "WaitForSingleObject") == 0)   { RET_STD(2, 0); }
    if (strcmp(fn, "WaitForSingleObjectEx") == 0) { RET_STD(3, 0); }
    if (strcmp(fn, "ExitProcess") == 0 || strcmp(fn, "TerminateProcess") == 0) {
        asi_log_warn("[ASI] Mod called %s() from 0x%08X - returning to host", fn, ret_va);
        r->eax = 0;
        return ASI_RET_SENTINEL;
    }
    if (strcmp(fn, "ReleaseMutex") == 0 || strcmp(fn, "SetEvent") == 0 || strcmp(fn, "ResetEvent") == 0) { RET_STD(1, 1); }
    if (strcmp(fn, "ReleaseSemaphore") == 0) { RET_STD(3, 1); }
    if (strcmp(fn, "CreateThread") == 0) {
        uint32_t start_va = g_arg(r, 2), param_va = g_arg(r, 3), tid_va = g_arg(r, 5);
        asi_log_info("CreateThread(start=0x%08X, param=0x%08X) from 0x%08X", start_va, param_va, ret_va);
        if (tid_va) g_wr32(tid_va, 0x4001u);
        if (s_host.spawn_asi_thread && start_va) {
            s_host.spawn_asi_thread(start_va, param_va);
        }
        RET_STD(6, 0x4001u);
    }
    if (strcmp(fn, "IsDebuggerPresent") == 0) { RET_STD(0, 0); }
    if (strcmp(fn, "IsProcessorFeaturePresent") == 0) { RET_STD(1, 1); }
    if (strcmp(fn, "SetUnhandledExceptionFilter") == 0) { RET_STD(1, 0); }
    if (strcmp(fn, "UnhandledExceptionFilter") == 0) { RET_STD(1, 1); }
    if (strcmp(fn, "AddVectoredExceptionHandler") == 0) { RET_STD(2, 0x5001u); }
    if (strcmp(fn, "EncodePointer") == 0 || strcmp(fn, "DecodePointer") == 0) { RET_STD(1, g_arg(r, 0)); }
    if (strcmp(fn, "GetStartupInfoW") == 0) {
        uint32_t p = g_arg(r, 0);
        if (p) { memset(g_ptr(p), 0, 68); g_wr32(p, 68); }
        RET_STD(1, 0);
    }
    if (strcmp(fn, "FormatMessageA") == 0) { RET_STD(7, 0); }
    if (strcmp(fn, "GetVersionExA") == 0) {
        uint32_t p = g_arg(r, 0);
        if (p) { g_wr32(p + 4, 6); g_wr32(p + 8, 1); g_wr32(p + 12, 7601); g_wr32(p + 16, 2); }
        RET_STD(1, 1);
    }

    /* --- USER32 --- */
    if (strcmp(fn, "GetAsyncKeyState") == 0) { RET_STD(1, 0); }
    if (strcmp(fn, "MessageBoxA") == 0) {
        const char *txt = g_cstr(g_arg(r, 1));
        const char *cap = g_cstr(g_arg(r, 2));
        asi_log_warn("[MessageBoxA] %s: %s", cap ? cap : "", txt ? txt : "");
        RET_STD(4, 1); /* IDOK */
    }
    if (strcmp(fn, "MessageBoxW") == 0) { RET_STD(4, 1); }
    if (strcmp(fn, "GetDesktopWindow") == 0) { RET_STD(0, 0x00010001u); }
    if (strcmp(fn, "FindWindowA") == 0) { RET_STD(2, 0x00010001u); }
    if (strcmp(fn, "GetClientRect") == 0 || strcmp(fn, "GetWindowRect") == 0) {
        uint32_t rc = g_arg(r, 1);
        if (rc) { g_wr32(rc, 0); g_wr32(rc + 4, 0); g_wr32(rc + 8, 1280); g_wr32(rc + 12, 720); }
        RET_STD(2, 1);
    }
    if (strcmp(fn, "GetWindowLongA") == 0) { RET_STD(2, 0); }
    if (strcmp(fn, "SetWindowLongA") == 0) { RET_STD(3, 0); }
    if (strcmp(fn, "SetWindowPos") == 0)   { RET_STD(7, 1); }
    if (strcmp(fn, "EnumWindows") == 0)    { RET_STD(2, 1); }
    if (strcmp(fn, "GetWindow") == 0)      { RET_STD(2, 0); }
    if (strcmp(fn, "IsWindowVisible") == 0){ RET_STD(1, 1); }
    if (strcmp(fn, "GetWindowThreadProcessId") == 0) {
        uint32_t p = g_arg(r, 1);
        if (p) g_wr32(p, (uint32_t)getpid());
        RET_STD(2, (uint32_t)(uintptr_t)pthread_self());
    }

    /* --- ADVAPI32 Crypto Stubs (e.g. BayviewForever hash check) --- */
    if (strcmp(fn, "CryptAcquireContextA") == 0) {
        uint32_t out = g_arg(r, 0);
        if (out) g_wr32(out, 0x6001u);
        RET_STD(5, 1);
    }
    if (strcmp(fn, "CryptCreateHash") == 0) {
        uint32_t out = g_arg(r, 4);
        if (out) g_wr32(out, 0x6002u);
        RET_STD(5, 1);
    }
    if (strcmp(fn, "CryptHashData") == 0) { RET_STD(4, 1); }
    if (strcmp(fn, "CryptGetHashParam") == 0) {
        uint32_t data_va = g_arg(r, 2), len_va = g_arg(r, 3);
        uint32_t len = len_va ? g_rd32(len_va) : 0;
        if (data_va && len > 0) memset(g_ptr(data_va), 0, len);
        RET_STD(5, 1);
    }
    if (strcmp(fn, "CryptDestroyHash") == 0)    { RET_STD(1, 1); }
    if (strcmp(fn, "CryptReleaseContext") == 0) { RET_STD(2, 1); }

    /* --- UCRT / MSVCRT (__cdecl!) --- */
    if (strcmp(fn, "malloc") == 0) { RET_CDECL(asi_heap_alloc(g_arg(r, 0), 0)); }
    if (strcmp(fn, "calloc") == 0) { RET_CDECL(asi_heap_alloc(g_arg(r, 0) * g_arg(r, 1), 1)); }
    if (strcmp(fn, "realloc") == 0){ RET_CDECL(asi_heap_realloc(g_arg(r, 0), g_arg(r, 1))); }
    if (strcmp(fn, "free") == 0)   { asi_heap_free(g_arg(r, 0)); RET_CDECL(0); }

    if (strcmp(fn, "memcpy") == 0) {
        uint32_t d = g_arg(r, 0), s = g_arg(r, 1), n = g_arg(r, 2);
        if (d && s && n) memcpy(g_ptr(d), g_ptr(s), n);
        RET_CDECL(d);
    }
    if (strcmp(fn, "memmove") == 0) {
        uint32_t d = g_arg(r, 0), s = g_arg(r, 1), n = g_arg(r, 2);
        if (d && s && n) memmove(g_ptr(d), g_ptr(s), n);
        RET_CDECL(d);
    }
    if (strcmp(fn, "memset") == 0) {
        uint32_t d = g_arg(r, 0), c = g_arg(r, 1), n = g_arg(r, 2);
        if (d && n) memset(g_ptr(d), (int)(c & 0xFFu), n);
        RET_CDECL(d);
    }
    if (strcmp(fn, "memcmp") == 0) {
        uint32_t a = g_arg(r, 0), b = g_arg(r, 1), n = g_arg(r, 2);
        RET_CDECL((a && b && n) ? memcmp(g_ptr(a), g_ptr(b), n) : 0);
    }
    if (strcmp(fn, "memchr") == 0) {
        uint32_t s = g_arg(r, 0), c = g_arg(r, 1), n = g_arg(r, 2);
        if (s && n) {
            uint8_t *p = (uint8_t *)memchr(g_ptr(s), (int)(c & 0xFFu), n);
            if (p) RET_CDECL((uint32_t)(p - s_mem));
        }
        RET_CDECL(0);
    }

    if (strcmp(fn, "strlen") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        RET_CDECL(s ? (uint32_t)strlen(s) : 0u);
    }
    if (strcmp(fn, "strnlen") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        uint32_t m = g_arg(r, 1);
        RET_CDECL(s ? (uint32_t)strnlen(s, m) : 0u);
    }
    if (strcmp(fn, "wcslen") == 0) {
        uint32_t p = g_arg(r, 0), n = 0;
        if (p) while (g_rd16(p + n * 2u) != 0) n++;
        RET_CDECL(n);
    }
    if (strcmp(fn, "wcsnlen") == 0) {
        uint32_t p = g_arg(r, 0), m = g_arg(r, 1), n = 0;
        if (p) while (n < m && g_rd16(p + n * 2u) != 0) n++;
        RET_CDECL(n);
    }
    if (strcmp(fn, "strcmp") == 0) {
        const char *a = g_cstr(g_arg(r, 0)), *b = g_cstr(g_arg(r, 1));
        RET_CDECL((a && b) ? strcmp(a, b) : 0);
    }
    if (strcmp(fn, "_stricmp") == 0) {
        const char *a = g_cstr(g_arg(r, 0)), *b = g_cstr(g_arg(r, 1));
        RET_CDECL((a && b) ? strcasecmp(a, b) : 0);
    }
    if (strcmp(fn, "strncmp") == 0) {
        const char *a = g_cstr(g_arg(r, 0)), *b = g_cstr(g_arg(r, 1));
        RET_CDECL((a && b) ? strncmp(a, b, g_arg(r, 2)) : 0);
    }
    if (strcmp(fn, "_strnicmp") == 0) {
        const char *a = g_cstr(g_arg(r, 0)), *b = g_cstr(g_arg(r, 1));
        RET_CDECL((a && b) ? strncasecmp(a, b, g_arg(r, 2)) : 0);
    }
    if (strcmp(fn, "strcpy") == 0) {
        uint32_t d = g_arg(r, 0), s = g_arg(r, 1);
        if (d && s) strcpy((char *)g_ptr(d), g_cstr(s));
        RET_CDECL(d);
    }
    if (strcmp(fn, "strncpy") == 0) {
        uint32_t d = g_arg(r, 0), s = g_arg(r, 1), n = g_arg(r, 2);
        if (d && s && n) strncpy((char *)g_ptr(d), g_cstr(s), n);
        RET_CDECL(d);
    }
    if (strcmp(fn, "strcat") == 0) {
        uint32_t d = g_arg(r, 0), s = g_arg(r, 1);
        if (d && s) strcat((char *)g_ptr(d), g_cstr(s));
        RET_CDECL(d);
    }
    if (strcmp(fn, "strncat") == 0) {
        uint32_t d = g_arg(r, 0), s = g_arg(r, 1), n = g_arg(r, 2);
        if (d && s) strncat((char *)g_ptr(d), g_cstr(s), n);
        RET_CDECL(d);
    }
    if (strcmp(fn, "strchr") == 0) {
        uint32_t s = g_arg(r, 0);
        const char *p = s ? strchr(g_cstr(s), (int)(g_arg(r, 1) & 0xFFu)) : NULL;
        RET_CDECL(p ? (uint32_t)((const uint8_t *)p - s_mem) : 0u);
    }
    if (strcmp(fn, "strrchr") == 0) {
        uint32_t s = g_arg(r, 0);
        const char *p = s ? strrchr(g_cstr(s), (int)(g_arg(r, 1) & 0xFFu)) : NULL;
        RET_CDECL(p ? (uint32_t)((const uint8_t *)p - s_mem) : 0u);
    }
    if (strcmp(fn, "strstr") == 0) {
        uint32_t a = g_arg(r, 0), b = g_arg(r, 1);
        const char *p = (a && b) ? strstr(g_cstr(a), g_cstr(b)) : NULL;
        RET_CDECL(p ? (uint32_t)((const uint8_t *)p - s_mem) : 0u);
    }
    if (strcmp(fn, "strcspn") == 0) {
        const char *a = g_cstr(g_arg(r, 0)), *b = g_cstr(g_arg(r, 1));
        RET_CDECL((a && b) ? (uint32_t)strcspn(a, b) : 0u);
    }
    if (strcmp(fn, "_strdup") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        if (!s) RET_CDECL(0);
        uint32_t len = (uint32_t)strlen(s) + 1u;
        uint32_t va = asi_heap_alloc(len, 0);
        if (va) memcpy(g_ptr(va), s, len);
        RET_CDECL(va);
    }
    if (strcmp(fn, "tolower") == 0) { RET_CDECL(tolower((int)g_arg(r, 0))); }
    if (strcmp(fn, "toupper") == 0) { RET_CDECL(toupper((int)g_arg(r, 0))); }
    if (strcmp(fn, "isdigit") == 0) { RET_CDECL(isdigit((int)g_arg(r, 0)) != 0); }
    if (strcmp(fn, "isspace") == 0) { RET_CDECL(isspace((int)g_arg(r, 0)) != 0); }
    if (strcmp(fn, "isxdigit") == 0){ RET_CDECL(isxdigit((int)g_arg(r, 0)) != 0); }
    if (strcmp(fn, "isalpha") == 0) { RET_CDECL(isalpha((int)g_arg(r, 0)) != 0); }
    if (strcmp(fn, "isalnum") == 0) { RET_CDECL(isalnum((int)g_arg(r, 0)) != 0); }
    if (strcmp(fn, "atoi") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        RET_CDECL(s ? (uint32_t)atoi(s) : 0u);
    }
    if (strcmp(fn, "strtol") == 0 || strcmp(fn, "strtoul") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        uint32_t end_va = g_arg(r, 1);
        int base = (int)g_arg(r, 2);
        char *ep = NULL;
        unsigned long v = s ? (fn[3] == 'u' ? strtoul(s, &ep, base) : (unsigned long)strtol(s, &ep, base)) : 0;
        if (end_va && ep) g_wr32(end_va, (uint32_t)((const uint8_t *)ep - s_mem));
        RET_CDECL((uint32_t)v);
    }
    if (strcmp(fn, "strtoll") == 0 || strcmp(fn, "strtoull") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        uint32_t end_va = g_arg(r, 1);
        int base = (int)g_arg(r, 2);
        char *ep = NULL;
        unsigned long long v = s ? strtoull(s, &ep, base) : 0;
        if (end_va && ep) g_wr32(end_va, (uint32_t)((const uint8_t *)ep - s_mem));
        r->edx = (uint32_t)(v >> 32);
        RET_CDECL((uint32_t)v);
    }
    if (strcmp(fn, "atof") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        r->st0 = s ? atof(s) : 0.0;
        r->has_st0 = 1;
        RET_CDECL(0);
    }
    if (strcmp(fn, "sqrt") == 0 || strcmp(fn, "tanh") == 0) {
        uint64_t bits = ((uint64_t)g_arg(r, 1) << 32) | g_arg(r, 0);
        double x = 0.0;
        memcpy(&x, &bits, 8);
        r->st0 = (fn[0] == 's') ? sqrt(x) : tanh(x);
        r->has_st0 = 1;
        RET_CDECL(0);
    }
    if (strcmp(fn, "lroundf") == 0) {
        uint32_t bits = g_arg(r, 0);
        float x = 0.0f;
        memcpy(&x, &bits, 4);
        RET_CDECL((uint32_t)(int32_t)lroundf(x));
    }
    if (strcmp(fn, "localeconv") == 0) { RET_CDECL(s_locale_va); }
    if (strcmp(fn, "getenv") == 0) {
        const char *k = g_cstr(g_arg(r, 0));
        const char *v = k ? getenv(k) : NULL;
        if (!v) RET_CDECL(0);
        uint32_t va = asi_heap_alloc((uint32_t)strlen(v) + 1u, 0);
        if (va) strcpy((char *)g_ptr(va), v);
        RET_CDECL(va);
    }
    if (strcmp(fn, "rand_s") == 0) {
        uint32_t out = g_arg(r, 0);
        if (out) g_wr32(out, (uint32_t)rand());
        RET_CDECL(0);
    }
    if (strcmp(fn, "_errno") == 0) { RET_CDECL(s_crt_errno_va); }
    if (strcmp(fn, "__acrt_iob_func") == 0) {
        uint32_t idx_iob = g_arg(r, 0);
        if (idx_iob > 2u) idx_iob = 2u;
        RET_CDECL(s_crt_iob_va + idx_iob * 64u);
    }
    if (strcmp(fn, "fopen") == 0) {
        const char *path = g_cstr(g_arg(r, 0));
        const char *mode = g_cstr(g_arg(r, 1));
        char host_p[1024], resolved[1024];
        map_win_path(path, host_p, sizeof(host_p));
        resolve_case_path(host_p, resolved, sizeof(resolved));
        FILE *fp = fopen(resolved, mode ? mode : "rb");
        if (!fp) RET_CDECL(0);
        for (int i = 3; i < MAX_CRT_FILES; i++) {
            if (!s_crt_files[i].in_use) {
                s_crt_files[i].in_use = 1;
                s_crt_files[i].fp = fp;
                s_crt_files[i].guest_struct_va = asi_heap_alloc(64, 1);
                RET_CDECL(s_crt_files[i].guest_struct_va);
            }
        }
        fclose(fp);
        RET_CDECL(0);
    }
    if (strcmp(fn, "fclose") == 0) {
        uint32_t gfp = g_arg(r, 0);
        for (int i = 3; i < MAX_CRT_FILES; i++) {
            if (s_crt_files[i].in_use && s_crt_files[i].guest_struct_va == gfp) {
                fclose(s_crt_files[i].fp);
                asi_heap_free(gfp);
                s_crt_files[i].in_use = 0;
                RET_CDECL(0);
            }
        }
        RET_CDECL(0);
    }
    if (strcmp(fn, "fread") == 0) {
        uint32_t buf = g_arg(r, 0), sz = g_arg(r, 1), cnt = g_arg(r, 2), gfp = g_arg(r, 3);
        FILE *fp = crt_lookup_fp(gfp);
        RET_CDECL((fp && buf) ? (uint32_t)fread(g_ptr(buf), sz, cnt, fp) : 0u);
    }
    if (strcmp(fn, "fwrite") == 0) {
        uint32_t buf = g_arg(r, 0), sz = g_arg(r, 1), cnt = g_arg(r, 2), gfp = g_arg(r, 3);
        FILE *fp = crt_lookup_fp(gfp);
        if (fp == stdout || fp == stderr) RET_CDECL(cnt);
        RET_CDECL((fp && buf) ? (uint32_t)fwrite(g_ptr(buf), sz, cnt, fp) : 0u);
    }
    if (strcmp(fn, "fgets") == 0) {
        uint32_t buf = g_arg(r, 0), n = g_arg(r, 1), gfp = g_arg(r, 2);
        FILE *fp = crt_lookup_fp(gfp);
        if (fp && buf && n > 0 && fgets((char *)g_ptr(buf), (int)n, fp)) RET_CDECL(buf);
        RET_CDECL(0);
    }
    if (strcmp(fn, "fputs") == 0) {
        const char *s = g_cstr(g_arg(r, 0));
        FILE *fp = crt_lookup_fp(g_arg(r, 1));
        if (fp == stdout || fp == stderr) {
            if (s) asi_log_info("[ASI stdout] %s", s);
            RET_CDECL(0);
        }
        RET_CDECL((fp && s) ? (uint32_t)fputs(s, fp) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "fputc") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 1));
        int ch = (int)g_arg(r, 0);
        if (fp == stdout || fp == stderr) RET_CDECL((uint32_t)ch);
        RET_CDECL(fp ? (uint32_t)fputc(ch, fp) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "fflush") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 0));
        if (fp && fp != stdout && fp != stderr) fflush(fp);
        RET_CDECL(0);
    }
    if (strcmp(fn, "getc") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 0));
        RET_CDECL(fp ? (uint32_t)fgetc(fp) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "ungetc") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 1));
        RET_CDECL(fp ? (uint32_t)ungetc((int)g_arg(r, 0), fp) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "fseek") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 0));
        RET_CDECL(fp ? (uint32_t)fseek(fp, (long)(int32_t)g_arg(r, 1), (int)g_arg(r, 2)) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "ftell") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 0));
        RET_CDECL(fp ? (uint32_t)ftell(fp) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "_lock_file") == 0 || strcmp(fn, "_unlock_file") == 0) { RET_CDECL(0); }
    if (strcmp(fn, "_fileno") == 0) {
        FILE *fp = crt_lookup_fp(g_arg(r, 0));
        RET_CDECL(fp ? (uint32_t)fileno(fp) : 0xFFFFFFFFu);
    }
    if (strcmp(fn, "_setmode") == 0) { RET_CDECL(0x8000u); }
    if (strcmp(fn, "__stdio_common_vsprintf") == 0) {
        /* (uint64_t opts, char *buf, size_t cnt, const char *fmt, locale_t, va_list) */
        uint32_t buf = g_arg(r, 2), cnt = g_arg(r, 3);
        const char *fmt = g_cstr(g_arg(r, 4));
        uint32_t ap = g_arg(r, 6);
        char tmp[1024];
        int n = guest_vsnprintf(tmp, sizeof(tmp), fmt, ap);
        if (buf && cnt > 0) {
            size_t cp = (size_t)n < (cnt - 1) ? (size_t)n : (cnt - 1);
            memcpy(g_ptr(buf), tmp, cp);
            g_ptr(buf)[cp] = '\0';
        }
        RET_CDECL((uint32_t)n);
    }
    if (strcmp(fn, "__stdio_common_vfprintf") == 0) {
        /* (uint64_t opts, FILE *fp, const char *fmt, locale_t, va_list) */
        FILE *fp = crt_lookup_fp(g_arg(r, 2));
        const char *fmt = g_cstr(g_arg(r, 3));
        uint32_t ap = g_arg(r, 5);
        char tmp[1024];
        int n = guest_vsnprintf(tmp, sizeof(tmp), fmt, ap);
        if (fp == stdout || fp == stderr) {
            while (n > 0 && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')) tmp[--n] = '\0';
            if (n > 0) asi_log_info("[ASI] %s", tmp);
        } else if (fp) {
            fwrite(tmp, 1, (size_t)n, fp);
            fflush(fp);
        }
        RET_CDECL((uint32_t)n);
    }
    if (strcmp(fn, "snprintf") == 0 || strcmp(fn, "_snprintf") == 0) {
        uint32_t buf = g_arg(r, 0), cnt = g_arg(r, 1);
        const char *fmt = g_cstr(g_arg(r, 2));
        int n = guest_vsnprintf((char *)g_ptr(buf), cnt, fmt, r->esp + 16u);
        RET_CDECL((uint32_t)n);
    }
    if (strcmp(fn, "sprintf") == 0) {
        uint32_t buf = g_arg(r, 0);
        const char *fmt = g_cstr(g_arg(r, 1));
        int n = guest_vsnprintf((char *)g_ptr(buf), 1024, fmt, r->esp + 12u);
        RET_CDECL((uint32_t)n);
    }
    if (strcmp(fn, "vsnprintf") == 0 || strcmp(fn, "_vsnprintf") == 0) {
        uint32_t buf = g_arg(r, 0), cnt = g_arg(r, 1);
        const char *fmt = g_cstr(g_arg(r, 2));
        uint32_t ap = g_arg(r, 3);
        int n = guest_vsnprintf((char *)g_ptr(buf), cnt, fmt, ap);
        RET_CDECL((uint32_t)n);
    }
    if (strcmp(fn, "_initialize_onexit_table") == 0 ||
        strcmp(fn, "_register_onexit_function") == 0 ||
        strcmp(fn, "_execute_onexit_table") == 0 ||
        strcmp(fn, "_initterm") == 0 ||
        strcmp(fn, "_initterm_e") == 0 ||
        strcmp(fn, "__setusermatherr") == 0) {
        RET_CDECL(0);
    }
    if (strcmp(fn, "_beginthreadex") == 0) {
        uint32_t start_va = g_arg(r, 2), param_va = g_arg(r, 3), tid_va = g_arg(r, 5);
        asi_log_info("_beginthreadex(start=0x%08X, param=0x%08X) from 0x%08X", start_va, param_va, ret_va);
        if (tid_va) g_wr32(tid_va, 0x4002u);
        if (s_host.spawn_asi_thread && start_va) {
            s_host.spawn_asi_thread(start_va, param_va);
        }
        RET_CDECL(0x4002u);
    }
    if (strcmp(fn, "_endthreadex") == 0) { RET_CDECL(0); }
    if (strcmp(fn, "strerror") == 0) {
        const char *s = strerror((int)g_arg(r, 0));
        uint32_t va = asi_heap_alloc((uint32_t)strlen(s) + 1u, 0);
        if (va) strcpy((char *)g_ptr(va), s);
        RET_CDECL(va);
    }
    if (strcmp(fn, "_assert") == 0) {
        asi_log_err("[ASI _assert] expr=%s file=%s line=%u",
                    g_cstr(g_arg(r, 0)) ? g_cstr(g_arg(r, 0)) : "?",
                    g_cstr(g_arg(r, 1)) ? g_cstr(g_arg(r, 1)) : "?",
                    g_arg(r, 2));
        RET_CDECL(0);
    }
    if (strcmp(fn, "abort") == 0 || strcmp(fn, "_exit") == 0 || strcmp(fn, "exit") == 0) {
        asi_log_err("[ASI] Mod called %s() from 0x%08X - returning to host", fn, ret_va);
        r->eax = 0;
        return ASI_RET_SENTINEL;
    }

    /* --- WS2_32 stubs (return graceful error if mod probes sockets) --- */
    if (strcmp(fn, "WSAStartup") == 0)      { RET_STD(2, 0); }
    if (strcmp(fn, "WSAGetLastError") == 0) { RET_STD(0, 10035u); /* WSAEWOULDBLOCK */ }
    if (strcmp(fn, "socket") == 0)          { RET_STD(3, 0xFFFFFFFFu); }
    if (strcmp(fn, "closesocket") == 0)     { RET_STD(1, 0); }
    if (strcmp(fn, "bind") == 0)            { RET_STD(3, 0xFFFFFFFFu); }
    if (strcmp(fn, "setsockopt") == 0)      { RET_STD(5, 0); }
    if (strcmp(fn, "ioctlsocket") == 0)     { RET_STD(3, 0); }
    if (strcmp(fn, "WSAIoctl") == 0)        { RET_STD(9, 0xFFFFFFFFu); }
    if (strcmp(fn, "sendto") == 0)          { RET_STD(6, 0xFFFFFFFFu); }
    if (strcmp(fn, "recvfrom") == 0)        { RET_STD(6, 0xFFFFFFFFu); }
    if (strcmp(fn, "select") == 0)          { RET_STD(5, 0); }

    /* --- Unimplemented API Fallback --- */
    if (!e->logged_unimpl) {
        e->logged_unimpl = 1;
        asi_log_warn("Unimplemented HLE API: %s!%s (called from 0x%08X) - returning 0",
                     e->dll, e->name, ret_va);
    }
    if (is_crt_dll(e->dll)) {
        RET_CDECL(0);
    }
    RET_STD(0, 0);

#undef RET_STD
#undef RET_CDECL
}
