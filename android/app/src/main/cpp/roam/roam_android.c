#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "roam_host_android.h"
#include "roam_core.h"
#include "roam_plat.h"
#include "roam_affinity.h"
#include "roam_powervr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <sched.h>
#include <dirent.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <jni.h>

ptrdiff_t *g_p_mem_base = NULL;
uint32_t  *g_p_esp = NULL;
uint32_t  *g_p_eax = NULL;
uint32_t  *g_p_ecx = NULL;
uint32_t  *g_p_edx = NULL;
uint32_t  *g_p_ebp = NULL;
uint32_t  *g_p_ebx = NULL;
uint32_t  *g_p_esi = NULL;
uint32_t  *g_p_edi = NULL;
recomp_func_t (*g_p_recomp_lookup)(uint32_t va) = NULL;
int (*g_p_guest_is_readable)(uint32_t va, uint32_t bytes) = NULL;
typedef uint8_t (*guest_mem_accessible_fn)(const void *self, uint32_t va, size_t bytes, uint8_t writable);
static guest_mem_accessible_fn s_fn_guest_accessible = NULL;
static void **s_pp_guest_memory = NULL;
static uintptr_t s_lib_base = 0;
static uint32_t s_display_w = 0, s_display_h = 0;

static int s_probe_pipe[2] = { -1, -1 };
static pthread_mutex_t s_probe_mu = PTHREAD_MUTEX_INITIALIZER;

static inline int probe_mem_readable(const void *ptr, size_t n) {
    if (!ptr) return 0;
    pthread_mutex_lock(&s_probe_mu);
    if (s_probe_pipe[0] < 0) {
        if (pipe2(s_probe_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
            pthread_mutex_unlock(&s_probe_mu);
            return 1;
        }
    }
    const uint8_t *p = (const uint8_t *)ptr;
    char dummy;
    ssize_t r1 = write(s_probe_pipe[1], p, 1);
    if (r1 != 1) {
        pthread_mutex_unlock(&s_probe_mu);
        return 0;
    }
    (void)read(s_probe_pipe[0], &dummy, 1);

    if (n > 1) {
        ssize_t r2 = write(s_probe_pipe[1], p + n - 1, 1);
        if (r2 != 1) {
            pthread_mutex_unlock(&s_probe_mu);
            return 0;
        }
        (void)read(s_probe_pipe[0], &dummy, 1);
    }
    pthread_mutex_unlock(&s_probe_mu);
    return 1;
}

int roam_android_mem_readable(uint32_t a, uint32_t n) {
    if (!g_p_mem_base || !*g_p_mem_base || !g_p_esp || *g_p_esp == 0) return 0;
    if (a < 0x00010000u || (uint64_t)a + n < 0x00010000u || (uint64_t)a + n > 0x100000000ULL) return 0;
    if (g_p_guest_is_readable) {
        return g_p_guest_is_readable(a, n);
    }
    if (s_fn_guest_accessible && s_pp_guest_memory) {
        void *mem = *s_pp_guest_memory;
        if (mem && *(uintptr_t *)mem == (uintptr_t)(*g_p_mem_base)) {
            return s_fn_guest_accessible(mem, a, (size_t)n, 0u) ? 1 : 0;
        }
    }
    return probe_mem_readable(GPTR(a), n);
}

int nfs_bind_to_perf_cores(void);

#ifdef ROAM_CORE_RUST
extern int nfs_rust_crowd_is_suppressed(void);
extern void nfs_rust_set_crowd_suppression(int enable);
extern uint32_t nfs_rust_filter_crowd_setting(uint32_t val);
extern int nfs_rust_apply_crowd_suppression(uint8_t *mem_base);
extern uint32_t nfs_rust_crowd_suppressed_frames(void);
extern int nfs_rust_apply_engine_pacing(uint8_t *mem_base, float real_elapsed);
extern void nfs_rust_set_contrast_mode(uint32_t mode);
extern uint32_t nfs_rust_get_contrast_mode(void);
#else
static inline void nfs_rust_set_crowd_suppression(int enable) { (void)enable; }
static inline int nfs_rust_apply_engine_pacing(uint8_t *mem_base, float real_elapsed) { (void)mem_base; (void)real_elapsed; return 0; }
#endif

#define G_CARPOOL_KB     0x007fa9d4u
#define CARPOOL_KB       48000
#define G_RIDE_SLOT_CAP  0x0061c744u

static int g_carpool_patched = 0;
static _Atomic int s_null_page_protected = 0;
static _Atomic int g_hooks_installed = 0;
static pthread_mutex_t s_hooks_mu = PTHREAD_MUTEX_INITIALIZER;
static void roam_android_install_hooks(void);


static void roam_android_protect_guest_null_page(void) {
    if (atomic_load(&s_null_page_protected) || !g_p_mem_base || !*g_p_mem_base) return;
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 16384;
    size_t null_protect_sz = ((65536 + (size_t)page_sz - 1) / (size_t)page_sz) * (size_t)page_sz;
    if (mprotect((void *)(*g_p_mem_base), null_protect_sz, PROT_READ | PROT_WRITE) == 0) {
        atomic_store(&s_null_page_protected, 1);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2",
            "Protected guest null page at %p (%zu bytes) as PROT_READ|PROT_WRITE for null-deref tolerance",
            (void *)(*g_p_mem_base), null_protect_sz);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2",
            "Failed to mprotect guest null page at %p: %s",
            (void *)(*g_p_mem_base), strerror(errno));
    }
}

static void *guest_null_page_guard_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 2000 && !atomic_load(&s_null_page_protected); i++) {
        roam_android_init_symbols();
        if (g_p_mem_base && *g_p_mem_base != 0) {
            roam_android_protect_guest_null_page();
            if (atomic_load(&s_null_page_protected)) break;
        }
        usleep(5000); // 5 ms
    }
    return NULL;
}

static struct sigaction s_old_sa_segv;
static _Atomic int s_segv_handler_installed = 0;

static void guest_null_segv_handler(int sig, siginfo_t *si, void *ucontext) {
    if (sig == SIGSEGV && si && g_p_mem_base && *g_p_mem_base != 0) {
        uintptr_t fault_addr = (uintptr_t)si->si_addr;
        uintptr_t base = (uintptr_t)(*g_p_mem_base);
        // Intercept any fault within the first 64KB of guest address space (null pointer field access)
        if (fault_addr >= base && fault_addr < base + 65536) {
            long page_sz = sysconf(_SC_PAGESIZE);
            if (page_sz <= 0) page_sz = 16384;
            size_t null_protect_sz = ((65536 + (size_t)page_sz - 1) / (size_t)page_sz) * (size_t)page_sz;
            if (mprotect((void *)base, null_protect_sz, PROT_READ | PROT_WRITE) == 0) {
                atomic_store(&s_null_page_protected, 1);
                __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                    "SIGSEGV recovered: protected guest null page at %p (%zu bytes) for fault addr %p",
                    (void *)base, null_protect_sz, (void *)fault_addr);
                return; // Safely re-execute the faulting instruction
            }
        }
    }

    // Forward other crashes to original signal handler (debuggerd / crash logger)
    if (s_old_sa_segv.sa_flags & SA_SIGINFO) {
        if (s_old_sa_segv.sa_sigaction) {
            s_old_sa_segv.sa_sigaction(sig, si, ucontext);
            return;
        }
    } else if (s_old_sa_segv.sa_handler && s_old_sa_segv.sa_handler != SIG_DFL && s_old_sa_segv.sa_handler != SIG_IGN) {
        s_old_sa_segv.sa_handler(sig);
        return;
    }

    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_guest_null_fault_guard(void) {
    if (atomic_exchange(&s_segv_handler_installed, 1) != 0) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = guest_null_segv_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &s_old_sa_segv) != 0) {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2", "Failed to install guest null SIGSEGV handler: %s", strerror(errno));
    } else {
        __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Installed guest null SIGSEGV recovery guard");
    }
}

static inline void roam_android_patch_carpool(void) {
    if (!g_carpool_patched && g_p_esp && *g_p_esp && rh_mem_committed(G_CARPOOL_KB, 12)) {
        for (int i = 0; i < 3; ++i) {
            uint32_t addr = G_CARPOOL_KB + (uint32_t)i * 4u;
            if (MR32(addr) < CARPOOL_KB) {
                MR32(addr) = CARPOOL_KB;
            }
        }
        if (rh_mem_committed(G_RIDE_SLOT_CAP, 1) && MR8(G_RIDE_SLOT_CAP) == 0x16) {
            MR8(G_RIDE_SLOT_CAP) = 0x7f; // expand CarLoadedRideInfoSlotPool from 22 to 127
        }
        if (rh_mem_committed(0x007F6DE8u, 8) && MR32(0x007F6DE8u) == 0x5b9d88b9u) {
            if (MR32(0x007F6DECu) < 524288u) {
                MR32(0x007F6DECu) = 524288u; // Expand LanguageMemoryPool headroom to 512KB for Spanish (280KB)
            }
        }
        g_carpool_patched = 1;
    }
}

int roam_android_init_symbols(void) {
    if (!g_p_mem_base) {
        void *h = dlopen("libnfsu2_android.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (!h) h = dlopen("libnfsu2_android.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!h) h = RTLD_DEFAULT;

        g_p_mem_base = (ptrdiff_t *)dlsym(h, "g_mem_base");
        g_p_recomp_lookup = (recomp_func_t (*)(uint32_t))dlsym(h, "recomp_lookup");
        g_p_esp = (uint32_t *)dlsym(h, "g_esp");
        g_p_eax = (uint32_t *)dlsym(h, "g_eax");
        g_p_ecx = (uint32_t *)dlsym(h, "g_ecx");
        g_p_edx = (uint32_t *)dlsym(h, "g_edx");
        g_p_ebp = (uint32_t *)dlsym(h, "g_ebp");
        g_p_ebx = (uint32_t *)dlsym(h, "g_ebx");
        g_p_esi = (uint32_t *)dlsym(h, "g_esi");
        g_p_edi = (uint32_t *)dlsym(h, "g_edi");
        g_p_guest_is_readable = (int (*)(uint32_t, uint32_t))dlsym(h, "guest_is_readable");
        s_fn_guest_accessible = (guest_mem_accessible_fn)dlsym(h, "_ZNK11GuestMemory10accessibleEjmb");
        if (!s_lib_base && g_p_recomp_lookup) {
            Dl_info dlinfo;
            if (dladdr((void *)g_p_recomp_lookup, &dlinfo) && dlinfo.dli_fbase) {
                s_lib_base = (uintptr_t)dlinfo.dli_fbase;
            }
        }
        if (s_lib_base && !s_pp_guest_memory) {
            s_pp_guest_memory = (void **)(s_lib_base + 0x97e7d10u);
        }
    }

    if (g_p_mem_base && *g_p_mem_base != 0) {
        roam_android_protect_guest_null_page();
    }

    if (!g_hooks_installed && g_p_recomp_lookup) {
        roam_android_install_hooks();
    }

    if (g_p_mem_base && *g_p_mem_base != 0 && g_p_esp && *g_p_esp != 0) {
        if (!g_carpool_patched) roam_android_patch_carpool();
        return 1;
    }
    return 0;
}

static int             g_fd = -1;
static pthread_t       g_thr;
static int             g_thr_on = 0;
static atomic_int      g_stop = 0;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static rc_session     *g_s = NULL;
static roam_plat_cfg   g_cfg;
static roam_plat_stats g_st;
static long            g_frame_count = 0;
static char            g_room_name[64] = "FREEROAM";
static char            g_player_name[64] = "Racer";
static char            g_peers_str[256] = "127.0.0.1:47211";
static int             g_ghost_mode = 1;

/* Quick Chat state */
static atomic_int      g_say_pick = -1;
static int             g_qc_open = -1;
static int64_t         g_qc_open_until = 0;
static int             g_qc_armed = 1;

/* Horn state */
static atomic_int      g_horn_held = 0;

int roam_android_horn_held(void) {
    return atomic_load(&g_horn_held);
}

static void roam_apply_gamepad_layout(int is_ps2);

static JavaVM         *g_jvm = NULL;
static jclass          g_client_class = NULL;
static jmethodID       g_mid_on_said = NULL;
static jmethodID       g_mid_on_send = NULL;
static uint32_t        g_hub_ip = 0;

void roam_plat_lock(void)   { pthread_mutex_lock(&g_mu); }
void roam_plat_unlock(void) { pthread_mutex_unlock(&g_mu); }

int64_t roam_plat_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

long roam_android_frame_no(void) {
    return g_frame_count;
}

uint32_t roam_plat_random(void) {
    static unsigned int seed = 123456789;
    seed = seed * 1664525u + 1013904223u;
    return seed ? seed : 1u;
}

void roam_plat_notice(const char *name, const char *what) {
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "Notice: %s%s", name ? name : "", what ? what : "");
}

int roam_plat_lobby(const char *json) { (void)json; return 0; }
int roam_plat_solid(void) { return g_ghost_mode ? 0 : 1; }
int roam_plat_quickchat(void) { return 1; }
int roam_plat_say_pick(void) { return atomic_exchange(&g_say_pick, -1); }

int roam_plat_said(const char *json) {
    if (!json || !g_jvm || !g_client_class || !g_mid_on_said) return 1;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_jvm)->GetEnv(g_jvm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) == 0) {
            attached = 1;
        } else {
            return 1;
        }
    }
    jstring jJson = (*env)->NewStringUTF(env, json);
    if (jJson) {
        (*env)->CallStaticVoidMethod(env, g_client_class, g_mid_on_said, jJson);
        (*env)->DeleteLocalRef(env, jJson);
    }
    if (attached) {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
    return 1;
}

void roam_plat_say_marks(const uint32_t *ids, const float *xys, int n) { (void)ids; (void)xys; (void)n; }

static uint32_t g_crew_gen = 1;
static char g_last_crew_str[256] = "";

uint32_t roam_plat_crews(uint32_t crews[RC_CREW_MAX], uint32_t rgb[RC_CREW_MAX]) {
    const char *env_crew = getenv("NFSU2_ROAM_CREW");
    if (!env_crew || !*env_crew) return 0;

    static const uint32_t default_rgb[RC_CREW_MAX] = { 0x3cd2ff, 0xff50c8, 0xa078ff, 0xff4646 };
    char copy[256];
    strncpy(copy, env_crew, sizeof copy - 1);
    copy[sizeof copy - 1] = 0;

    char *token = strtok(copy, ",;");
    int count = 0;
    while (token && count < RC_CREW_MAX) {
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token);
        while (end > token && (*(end - 1) == ' ' || *(end - 1) == '\t')) *(--end) = 0;
        if (*token) {
            uint32_t h = rc_room_hash(token);
            if (h) {
                crews[count] = h;
                rgb[count] = default_rgb[count % 4];
                count++;
            }
        }
        token = strtok(NULL, ",;");
    }
    for (int i = count; i < RC_CREW_MAX; i++) {
        crews[i] = 0;
        rgb[i] = 0;
    }
    if (strcmp(g_last_crew_str, env_crew) != 0) {
        strncpy(g_last_crew_str, env_crew, sizeof g_last_crew_str - 1);
        g_crew_gen++;
    }
    return g_crew_gen;
}

int roam_plat_test_mode(void) { return 0; }
int roam_plat_friend(const char *json) { (void)json; return 0; }
int roam_plat_battle(const char *json) { (void)json; return 0; }
int roam_plat_flying(const char *json) { (void)json; return 0; }
uint32_t roam_plat_room(char name[64]) { (void)name; return 0; }
uint32_t roam_plat_name(char out[33]) { (void)out; return 0; }

void look_eye(float *e, float *f) {
    if (g_p_mem_base && *g_p_mem_base && g_p_esp && *g_p_esp) {
        if (!rh_mem_committed(SPLIT_PLAYERS_INDEX, 4)) return;
        uint32_t pl = MR32(SPLIT_PLAYERS_INDEX);
        uint32_t mover = (pl && rh_mem_committed(pl + 0x238u, 4)) ? MR32(pl + 0x238u) : 0;
        uint32_t cam = (mover && rh_mem_committed(mover + 0x40u, 4)) ? MR32(mover + 0x40u) : 0;
        if (cam && rh_mem_committed(cam + 0x40u, 32)) {
            if (e) memcpy(e, GPTR(cam + 0x40u), 12);
            if (f) memcpy(f, GPTR(cam + 0x50u), 12);
        }
    }
}

const char *roam_ip_str(uint32_t ip, char *b) {
    snprintf(b, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
    return b;
}

void roam_plat_get_stats(roam_plat_stats *o) {
    roam_plat_lock();
    *o = g_st;
    roam_plat_unlock();
}

const char *roam_opt(const char *name) {
    if (!name) return NULL;
    if (!strcmp(name, "Room") || !strcmp(name, "NFSU2_ROAM_ROOM")) return g_room_name;
    if (!strcmp(name, "Name") || !strcmp(name, "NFSU2_ROAM_NAME")) return g_player_name;
    if (!strcmp(name, "Port") || !strcmp(name, "NFSU2_ROAM_PORT")) return "47210";
    if (!strcmp(name, "Peers") || !strcmp(name, "NFSU2_ROAM_PEERS")) return g_peers_str;
    if (!strcmp(name, "Enable") || !strcmp(name, "NFSU2_ROAM")) return "1";
    if (!strcmp(name, "Collide") || !strcmp(name, "NFSU2_ROAM_COLLIDE")) return g_ghost_mode ? "0" : "1";
    if (!strcmp(name, "NFSU2_ROAM_HUB")) return g_hub_ip ? "1" : "0";
    return getenv(name);
}

void roam_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "%s", buf);
}

rh_file *rh_fopen(const char *path) { return (rh_file *)fopen(path, "w"); }
int rh_fprintf(rh_file *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf((FILE *)f, fmt, ap);
    va_end(ap);
    return r;
}
void rh_fflush(rh_file *f) { fflush((FILE *)f); }

/* Sending packets over UDP */
static void do_send(uint32_t ip, uint16_t port, const uint8_t *buf, int len) {
    if (g_fd < 0 || !buf || len <= 0) return;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);
    ssize_t n = sendto(g_fd, buf, (size_t)len, 0, (struct sockaddr *)&sa, sizeof sa);
    if (n == len) {
        g_st.sent++;
        g_st.sent_bytes += (uint64_t)len;
    } else {
        g_st.send_err++;
    }
}

static void io_send(void *u, uint32_t ip, uint16_t port, const uint8_t *buf, int len) {
    (void)u;
    if (!buf || len <= 0) return;

    if (g_jvm && g_client_class && g_mid_on_send) {
        JNIEnv *env = NULL;
        int attached = 0;
        jint res = (*g_jvm)->GetEnv(g_jvm, (void **)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) == 0) {
                attached = 1;
            }
        }
        if (env) {
            jbyteArray jData = (*env)->NewByteArray(env, len);
            if (jData) {
                (*env)->SetByteArrayRegion(env, jData, 0, len, (const jbyte *)buf);
                (*env)->CallStaticVoidMethod(env, g_client_class, g_mid_on_send, (jint)ip, (jint)port, jData);
                (*env)->DeleteLocalRef(env, jData);
            }
            if (attached) {
                (*g_jvm)->DetachCurrentThread(g_jvm);
            }
            g_st.sent++;
            g_st.sent_bytes += (uint64_t)len;
            return;
        }
    }

    do_send(ip, port, buf, len);
}

static void io_broadcast(void *u, uint16_t port, const uint8_t *buf, int len) {
    (void)u;
    io_send(NULL, 0xFFFFFFFFu, port, buf, len);
}

extern void roam_game_event(int ev, int peer, const char *text);
static void io_event(void *u, int ev, int peer, const char *text) {
    (void)u;
    roam_game_event(ev, peer, text);
}

/* Background Network Thread */
static void *net_thread(void *arg) {
    (void)arg;
    uint8_t buf[1500];
    int64_t next_tick = 0;
    JNIEnv *env = NULL;
    if (g_jvm) {
        (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
    }

    while (!atomic_load(&g_stop)) {
        struct pollfd pf = { g_fd, POLLIN, 0 };
        poll(&pf, 1, 5);
        int64_t now = roam_plat_now_us();

        for (int k = 0; k < 256; k++) {
            struct sockaddr_in sa;
            socklen_t sl = sizeof sa;
            ssize_t n = recvfrom(g_fd, buf, sizeof buf, MSG_DONTWAIT, (struct sockaddr *)&sa, &sl);
            if (n < 0) break;
            if (sa.sin_family != AF_INET) continue;
            uint32_t ip = ntohl(sa.sin_addr.s_addr);
            uint16_t port = ntohs(sa.sin_port);

            roam_plat_lock();
            g_st.recv++;
            g_st.recv_bytes += (uint64_t)n;
            if (g_s) {
                if (n > RC_MAX_DGRAM) {
                    g_s->rx++;
                    g_s->rx_bad++;
                } else {
                    rc_receive(g_s, ip, port, buf, (int)n, now);
                }
            }
            roam_plat_unlock();
        }

        if (now >= next_tick) {
            roam_plat_lock();
            if (g_s) rc_tick(g_s, now);
            roam_plat_unlock();
            next_tick = now + 10000; // 10ms
        }
    }
    if (g_jvm && env) {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
    return NULL;
}

int roam_plat_start(const roam_plat_cfg *cfg, rc_session *s) {
    roam_plat_lock();
    if (g_fd >= 0) {
        roam_plat_unlock();
        return 0;
    }
    g_s = s;
    if (cfg) g_cfg = *cfg;
    if (g_hub_ip) {
        s->conf.hub_ip = g_hub_ip;
        s->conf.hub_port = cfg && cfg->port ? cfg->port : 47210;
        s->conf.snap_states = 1;
        s->hub_hello_tx = INT64_MIN / 4;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "Enabled Free Roam Hub: %08x:%u", g_hub_ip, s->conf.hub_port);
    }

    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Roam", "Failed creating UDP socket: %s", strerror(errno));
        roam_plat_unlock();
        return -1;
    }

    int broadcast = 1;
    setsockopt(g_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast);
    int reuse = 1;
    setsockopt(g_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(cfg && cfg->port ? cfg->port : 47210);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2-Roam", "Failed binding UDP 47210: %s", strerror(errno));
    }

    s->io.send = io_send;
    s->io.broadcast = io_broadcast;
    s->io.event = io_event;

    atomic_store(&g_stop, 0);
    g_thr_on = (pthread_create(&g_thr, NULL, net_thread, NULL) == 0);

    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "Native Free Roam network started on UDP %u", ntohs(sa.sin_port));
    roam_plat_unlock();
    return 0;
}

void roam_plat_stop(uint8_t bye_reason) {
    (void)bye_reason;
    roam_plat_lock();
    if (g_fd < 0) {
        roam_plat_unlock();
        return;
    }
    atomic_store(&g_stop, 1);
    int fd = g_fd;
    g_fd = -1;
    close(fd);
    roam_plat_unlock();

    if (g_thr_on) {
        pthread_join(g_thr, NULL);
        g_thr_on = 0;
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "Native Free Roam network stopped");
}

int roam_plat_parse_peers(const char *list, int test_mode, uint16_t defport, uint32_t *ip, uint16_t *port, int max) {
    (void)test_mode;
    if (!list || !*list || max <= 0) return 0;
    int count = 0;
    char copy[512];
    strncpy(copy, list, sizeof copy - 1);
    copy[sizeof copy - 1] = 0;

    char *token = strtok(copy, ",; ");
    while (token && count < max) {
        char *colon = strchr(token, ':');
        uint16_t p = defport;
        if (colon) {
            *colon = 0;
            p = (uint16_t)atoi(colon + 1);
        }
        struct in_addr addr;
        if (inet_pton(AF_INET, token, &addr) > 0) {
            ip[count] = ntohl(addr.s_addr);
            port[count] = p;
            count++;
        }
        token = strtok(NULL, ",; ");
    }
    return count;
}

/* Free Roam Engine Per-Frame Hook */
extern void roam_game_frame(CPU *c);

/* Weak dispatch_patch fallback if host engine does not export it */
__attribute__((weak)) void dispatch_patch(uint32_t a, void (*f)(CPU *)) {
    (void)a; (void)f;
}

#include "roam_patches.h"

static void framerate_pacing_fix(void) {
    static int s_crowd_env_checked = 0;
    if (!s_crowd_env_checked) {
        s_crowd_env_checked = 1;
        const char *env_crowd = getenv("NFSU2_CROWD");
        int allow_crowd = (env_crowd && (strcmp(env_crowd, "1") == 0 || strcasecmp(env_crowd, "on") == 0));
        nfs_rust_set_crowd_suppression(!allow_crowd);
    }
    if (g_p_mem_base && *g_p_mem_base) {
        static int64_t s_last_tick_us = 0;
        int64_t now_us = roam_plat_now_us();
        float real_elapsed = (s_last_tick_us > 0) ? (float)(now_us - s_last_tick_us) / 1000000.0f : 0.016666668f;
        s_last_tick_us = now_us;
        nfs_rust_apply_engine_pacing((uint8_t *)(*g_p_mem_base), real_elapsed);
    }
}

static volatile uint32_t s_target_fps = 0;

__attribute__((visibility("default"))) void nfs_roam_set_frame_limit(uint32_t fps) {
    s_target_fps = (fps >= 30 && fps <= 240) ? fps : 0;
}

static void pace_frame(void) {
    /* Pacing is handled cooperatively by host FramePacer in guest_runtime via yieldGuestMachine.
     * Avoid blocking nanosleep here which double-waits and starves guest background threads. */
}

static float g_stick_x = 0.f;
static float g_stick_y = 0.f;

/* =========================================================================
 * ARM64 Runtime Inline Hooking & Right Stick Camera / Garage Viewer Engine
 * ========================================================================= */

typedef void (*recomp_void_func_t)(void);
typedef void (*recomp_hook_func_t)(uint32_t va);

static recomp_void_func_t s_orig_4a5d90 = NULL;
static recomp_void_func_t s_orig_5c7ec0 = NULL;
static recomp_void_func_t s_orig_5caad0 = NULL;
static recomp_void_func_t s_orig_4172d0 = NULL;
static recomp_void_func_t s_orig_5ff490 = NULL;
static recomp_void_func_t s_orig_492e10 = NULL;
static recomp_void_func_t s_orig_61ad80 = NULL;
static recomp_void_func_t s_orig_483b40 = NULL;
static recomp_void_func_t s_orig_54ee40 = NULL;
static recomp_void_func_t s_orig_49b960 = NULL;
static recomp_void_func_t s_orig_49b930 = NULL;
static recomp_void_func_t s_orig_4b6510 = NULL;
static recomp_void_func_t s_orig_49b8a0 = NULL;
static recomp_void_func_t s_orig_49b8e0 = NULL;
static recomp_hook_func_t s_orig_recomp_hook = NULL;

typedef uint32_t (*dispatch_d3d9_func_t)(uint32_t token, const uint32_t *args, uint32_t *argumentCount);
static dispatch_d3d9_func_t s_orig_dispatch_d3d9 = NULL;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_num;
    uint32_t refresh_den;
    uint32_t bits_per_pixel;
    uint8_t  interlaced;
    uint8_t  pad[3];
} DxvkWsiMode;

typedef void (*configure_res_modes_fn)(void);
typedef int (*nfs_android_res_fn)(void);
typedef uint8_t (*wsi_get_display_mode_fn)(void *monitor, uint32_t mode_num, DxvkWsiMode *mode);
typedef void (*widescreen_set_res_fn)(unsigned width, unsigned height);
typedef void (*widescreen_apply_fps_fn)(void);
typedef void (*set_guest_frame_limit_fn)(uint32_t fps);

static configure_res_modes_fn s_orig_configure_res_modes = NULL;
static nfs_android_res_fn s_orig_nfs_android_res = NULL;
static wsi_get_display_mode_fn s_orig_wsi_get_display_mode = NULL;
static widescreen_set_res_fn s_orig_widescreen_set_res = NULL;
static widescreen_apply_fps_fn s_orig_widescreen_apply_fps = NULL;
static set_guest_frame_limit_fn s_fn_set_guest_frame_limit = NULL;

typedef struct {
    uint32_t w;
    uint32_t h;
    uint32_t pct;
} CustomResMode;

static CustomResMode s_custom_modes[6];
static uint32_t s_custom_mode_count = 0;
static uint32_t s_res_active_slot = 2;
static int s_custom_modes_ready = 0;
static int s_boot_selftest_pending = 0;
static uint32_t *s_p_guest_render_modes = NULL;
static uint8_t *s_p_first_game_res = NULL;

static recomp_void_func_t arm64_inline_hook(void *target_func, void *hook_func) {
    if (!target_func || !hook_func) return NULL;
    uint32_t *target = (uint32_t *)target_func;

    if (target[0] == 0x58000050u && target[1] == 0xd61f0200u) {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2-Hook",
                            "Target %p already inline-hooked, skipping duplicate hook", target_func);
        return NULL;
    }

    static uint8_t *s_tramp_page = NULL;
    static size_t s_tramp_offset = 0;
    if (!s_tramp_page) {
        s_tramp_page = (uint8_t *)mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC,
                                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (s_tramp_page == MAP_FAILED) {
            s_tramp_page = NULL;
            __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Hook", "mmap trampoline page failed");
            return NULL;
        }
    }
    if (s_tramp_offset + 64 > 8192) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Hook", "trampoline page exhausted");
        return NULL;
    }

    uint8_t *tramp = s_tramp_page + s_tramp_offset;
    s_tramp_offset += 64;

    // 1. Copy first 4 instructions (16 bytes) and relocate any PC-relative ADRP via literal slots
    uint32_t *t_insns = (uint32_t *)tramp;
    size_t lit_idx = 0;
    for (size_t i = 0; i < 4; i++) {
        uint32_t insn = target[i];
        if ((insn & 0x9f000000u) == 0x90000000u && lit_idx < 4) {
            uint32_t rd = insn & 0x1fu;
            uint32_t immlo = (insn >> 29) & 0x3u;
            uint32_t immhi = (insn >> 5) & 0x7ffffu;
            int64_t imm21 = (int64_t)((immhi << 2) | immlo);
            if (imm21 & (1LL << 20)) imm21 -= (1LL << 21);
            uint64_t orig_pc = (uint64_t)((uintptr_t)&target[i]);
            uint64_t target_page = (orig_pc & ~0xfffULL) + (uint64_t)(imm21 << 12);
            uint64_t lit_addr = (uint64_t)((uintptr_t)(tramp + 32 + lit_idx * 8));
            *(uint64_t *)(tramp + 32 + lit_idx * 8) = target_page;
            uint64_t tramp_pc = (uint64_t)((uintptr_t)(tramp + i * 4));
            int64_t delta_words = ((int64_t)lit_addr - (int64_t)tramp_pc) >> 2;
            t_insns[i] = 0x58000000u | (((uint32_t)delta_words & 0x7ffffu) << 5) | rd;
            lit_idx++;
        } else {
            t_insns[i] = insn;
        }
    }

    // 2. Append branch to target + 16 (4 instructions)
    uint32_t *t_code = (uint32_t *)(tramp + 16);
    t_code[0] = 0x58000050; // ldr x16, [pc, #8]
    t_code[1] = 0xd61f0200; // br x16
    *(uint64_t *)(tramp + 24) = (uint64_t)((uintptr_t)target + 16);

    __builtin___clear_cache((char *)tramp, (char *)tramp + 64);

    // 3. Make target code page writable
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 16384;
    uintptr_t page_start = (uintptr_t)target & ~((uintptr_t)page_sz - 1);
    if (mprotect((void *)page_start, page_sz * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Hook", "mprotect failed for target %p", target);
        return NULL;
    }

    // 4. Overwrite target with jump to hook_func
    target[0] = 0x58000050; // ldr x16, [pc, #8]
    target[1] = 0xd61f0200; // br x16
    *(uint64_t *)(&target[2]) = (uint64_t)((uintptr_t)hook_func);

    __builtin___clear_cache((char *)target, (char *)target + 16);

    return (recomp_void_func_t)tramp;
}

#define VW_TURN      0x248u        /* float: the viewer's turn speed (60 deg/sec at 1.0) */
#define VW_TILT      0x24cu        /* float: its camera's tilt speed */
#define VW_ANGLE     0x274u        /* float: the car's angle, degrees */
#define G_FE_BUSY    0x00838578u   /* the front end takes no input now */
#define G_MOUSE_MID  0x008763f7u   /* byte: the mouse's middle button is down */
#define G_SHOWCASE   0x008363fcu   /* byte: the showcase turns the car */
#define G_VIEW_IDLE  0x00837aa0u   /* when the viewer was last turned */
#define G_REALTIMER  0x008651acu

static void hook_sub_004a5d90(void) {
    static uint32_t turning = 0;
    uint32_t v = (g_p_ecx && g_p_mem_base && *g_p_mem_base) ? *g_p_ecx : 0;

    if (s_orig_4a5d90) {
        s_orig_4a5d90();
    }

    if (!g_p_mem_base || !*g_p_mem_base || !v) return;
    if (!rh_mem_committed(v, 0x280)) return;

    static void (*s_garage_apply)(uint32_t, void *) = NULL;
    if (!s_garage_apply) {
        void *h = dlopen("libgame_core.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libgame_core.so", RTLD_NOW | RTLD_GLOBAL);
        if (h) s_garage_apply = (void (*)(uint32_t, void *))dlsym(h, "nfs_garage_apply_viewer");
        if (!s_garage_apply) s_garage_apply = (void (*)(uint32_t, void *))dlsym(RTLD_DEFAULT, "nfs_garage_apply_viewer");
    }
    if (s_garage_apply) {
        s_garage_apply(v, (void *)(*g_p_mem_base));
        return;
    }

    if (rh_mem_committed(G_MOUSE_MID, 1) && MR8(G_MOUSE_MID)) {
        turning = 0;
        return;
    }

    float x = g_stick_x;
    float y = g_stick_y;
    float r = sqrtf(x * x + y * y);
    if (r < 0.15f) {
        x = 0.f;
        y = 0.f;
    }

    int busy = (rh_mem_committed(G_FE_BUSY, 4) && MR32(G_FE_BUSY) != 0);

    if (x == 0.f && y == 0.f) {
        if (turning == v) {
            MRF32(v + VW_TURN) = 0.f;
            MRF32(v + VW_TILT) = 0.f;
        }
        turning = 0;
        return;
    }

    MRF32(v + VW_TURN) = x;
    MRF32(v + VW_TILT) = y;
    if (rh_mem_committed(G_SHOWCASE, 1)) {
        MR8(G_SHOWCASE) = 0;
    }
    if (rh_mem_committed(G_VIEW_IDLE, 4) && rh_mem_committed(G_REALTIMER, 4)) {
        MR32(G_VIEW_IDLE) = MR32(G_REALTIMER);
    }
    turning = v;
}

#define CAR_POS      0x60u         /* the car's centre, world */
#define CAM_POS      0x40u         /* the camera's eye, world */
#define CAM_FWD      0x50u         /* its forward */
#define CAM_TARGET   0x60u         /* the point it looks at */
#define CAM_TURNED   0x70u         /* its bytes the turn changes: the matrix, those three */
#define NCAM         2

#define STICK_ON     0.20f
#define STICK_FULL   0.85f
#define TAU_S        0.09f
#define BACK_RAD     2.4f
#define SNAP_IN      0.44f
#define SNAP_OUT     0.61f
#define SNAP_R       0.50f
#define SNAP_R_OFF   0.40f
#define IN_CAR_M     3.0f
#define REST_RAD     0.0005f
#define PI_F         3.14159265358979323846f

static float    s_look_yaw = 0.f;
static int      s_look_back = 0;
static double   s_look_last_t = 0.0;
static int      s_look_active = 0;
static float    s_pivot_x = 0.f, s_pivot_y = 0.f;
static uint32_t s_turned_cams[NCAM] = {0};
static uint8_t  s_keep_cam[NCAM][CAM_TURNED];
static uint8_t  s_made_cam[NCAM][CAM_TURNED];
static int      s_num_cams_turned = 0;

static uint32_t get_driving_car(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return 0;
    if (!rh_mem_committed(0x008900ac, 4)) return 0; // SPLIT_PLAYERS_INDEX
    uint32_t pl = MR32(0x008900ac);
    if (!pl || pl >= 0x7fffffff || !rh_mem_committed(pl + 4, 4)) return 0;
    uint32_t car = MR32(pl + 4);
    if (!car || car >= 0x7fffffff || !rh_mem_committed(car + CAR_POS, 16)) return 0;

    // Check mover type (pl + 0x238 is mover; mover + 0xc is type, 1 = MOVER_DRIVE)
    if (rh_mem_committed(pl + 0x238, 4)) {
        uint32_t mv = MR32(pl + 0x238);
        if (mv && mv < 0x7fffffff && rh_mem_committed(mv + 0x0c, 4)) {
            uint32_t mv_type = MR32(mv + 0x0c);
            if (mv_type != 1) return 0;
        }
    }
    return car;
}

static float get_car_speed_mps(uint32_t car) {
    if (!car || !rh_mem_committed(car + 0x430, 4)) return 0.0f;
    float spd = MRF32(car + 0x430);
    if (spd >= 0.0f && spd < 200.0f) return spd;
    if (rh_mem_committed(car + 0x70, 12)) {
        float vx = MRF32(car + 0x70), vy = MRF32(car + 0x74), vz = MRF32(car + 0x78);
        float len = sqrtf(vx * vx + vy * vy + vz * vz);
        if (len >= 0.0f && len < 200.0f) return len;
    }
    return 0.0f;
}

static float compute_stick_yaw(int can, int *back) {
    *back = 0;
    if (!can) return 0.f;
    float x = g_stick_x, y = g_stick_y;
    float r = hypotf(x, y);
    if (r < STICK_ON) return 0.f;

    float off = PI_F - fabsf(atan2f(x, -y));
    if (s_look_back ? (off < SNAP_OUT && r > SNAP_R_OFF) : (off < SNAP_IN && r > SNAP_R)) {
        *back = 1;
        return 0.f;
    }
    if (off < (s_look_back ? SNAP_OUT : SNAP_IN)) return 0.f;

    float k = fminf(1.f, (r - STICK_ON) / (STICK_FULL - STICK_ON));
    float dir = atan2f(x, -y);
    int side = (fabsf(s_look_yaw) > 0.05f) ? (s_look_yaw > 0.f ? 1 : -1) : (x >= 0.f ? 1 : -1);
    if (fabsf(dir) > BACK_RAD && (dir > 0.f ? 1 : -1) != side) {
        dir += side * 2.f * PI_F;
    }
    return dir * k;
}

static void turn_cam_xy(float *p, int pt) {
    float c = cosf(s_look_yaw), s = sinf(s_look_yaw);
    float ox = pt ? s_pivot_x : 0.f, oy = pt ? s_pivot_y : 0.f;
    float x = p[0] - ox, y = p[1] - oy;
    p[0] = ox + x * c + y * s;
    p[1] = oy - x * s + y * c;
}

static void turn_cam_matrix(uint32_t cam) {
    float c = cosf(s_look_yaw), s = -sinf(s_look_yaw);
    float *M = (float *)GPTR(cam);
    float tx = s_pivot_x - (s_pivot_x * c + s_pivot_y * s);
    float ty = s_pivot_y - (-s_pivot_x * s + s_pivot_y * c);
    float r0[4], r1[4];
    for (int k = 0; k < 4; k++) {
        r0[k] = c * M[k] - s * M[4 + k];
        r1[k] = s * M[k] + c * M[4 + k];
    }
    for (int k = 0; k < 4; k++) {
        M[12 + k] += tx * M[k] + ty * M[4 + k];
        M[k] = r0[k];
        M[4 + k] = r1[k];
    }
}

static void restore_turned_cameras(void) {
    for (int i = 0; i < s_num_cams_turned; i++) {
        uint32_t cam = s_turned_cams[i];
        if (cam && rh_mem_committed(cam, CAM_TURNED)) {
            if (memcmp(GPTR(cam), s_made_cam[i], CAM_TURNED) == 0) {
                memcpy(GPTR(cam), s_keep_cam[i], CAM_TURNED);
            }
        }
    }
    s_num_cams_turned = 0;
}

static void turn_one_camera(uint32_t cam) {
    if (!cam || !rh_mem_committed(cam, CAM_TURNED) || s_num_cams_turned >= NCAM) return;
    for (int i = 0; i < s_num_cams_turned; i++) {
        if (s_turned_cams[i] == cam) return;
    }
    memcpy(s_keep_cam[s_num_cams_turned], GPTR(cam), CAM_TURNED);
    turn_cam_matrix(cam);
    turn_cam_xy((float *)GPTR(cam + CAM_POS), 1);
    turn_cam_xy((float *)GPTR(cam + CAM_FWD), 0);
    turn_cam_xy((float *)GPTR(cam + CAM_TARGET), 1);
    memcpy(s_made_cam[s_num_cams_turned], GPTR(cam), CAM_TURNED);
    s_turned_cams[s_num_cams_turned++] = cam;
}

static void look_process_view(uint32_t view_id, uint32_t cam) {
    if (view_id != 1u) {
        if (s_look_active && cam) turn_one_camera(cam);
        return;
    }

    restore_turned_cameras();

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec * 1e-9;
    float dt = (s_look_last_t > 0.0) ? (float)(t - s_look_last_t) : 0.016f;
    s_look_last_t = t;
    if (dt > 0.25f) dt = 0.25f;

    uint32_t car = get_driving_car();
    int back = 0;
    float want = compute_stick_yaw(car != 0, &back);

    if (back) {
        if (!s_look_back) s_look_yaw = (s_look_yaw < -0.05f) ? -PI_F : PI_F;
        s_look_back = 1;
        s_look_active = 1;
    } else {
        if (s_look_back) {
            s_look_back = 0;
            if (want == 0.f) s_look_yaw = 0.f;
        }
        float d = want - s_look_yaw;
        while (d > PI_F) d -= 2.f * PI_F;
        while (d < -PI_F) d += 2.f * PI_F;
        s_look_yaw += d * (1.f - expf(-dt / TAU_S));
        while (s_look_yaw > PI_F) s_look_yaw -= 2.f * PI_F;
        while (s_look_yaw < -PI_F) s_look_yaw += 2.f * PI_F;
    }

    if (!car || (want == 0.f && !s_look_back && fabsf(s_look_yaw) < REST_RAD)) {
        s_look_yaw = 0.f;
        s_look_active = 0;
        return;
    }

    float cx = MRF32(car + CAR_POS), cy = MRF32(car + CAR_POS + 4u);
    if (cam && rh_mem_committed(cam + CAM_POS, 12)) {
        float ex = MRF32(cam + CAM_POS), ey = MRF32(cam + CAM_POS + 4u);
        if (hypotf(ex - cx, ey - cy) < IN_CAR_M) {
            cx = ex;
            cy = ey;
        }
    }
    s_pivot_x = cx;
    s_pivot_y = cy;
    s_look_active = 1;
    turn_one_camera(cam);
}

static float get_configured_draw_distance(void) {
    const char *dd = getenv("NFSU2_DRAW_DISTANCE");
    if (dd && *dd) {
        float val = strtof(dd, NULL);
        if (val > 0.0f) return val;
    }
    return 0.0f;
}

static void hook_sub_005c7ec0(void) {
    uint32_t view_id = 0;
    uint32_t cam = 0;
    float max_far = 0.0f;
    float dd = get_configured_draw_distance();
    if (g_p_mem_base && *g_p_mem_base && g_p_esp && *g_p_esp) {
        uint32_t esp = *g_p_esp;
        if (rh_mem_committed(esp + 4, 4)) {
            uint32_t view = MR32(esp + 4);
            if (view && rh_mem_committed(view + 4, 0x40)) {
                view_id = MR32(view + 4);
                cam = MR32(view + 0x40);
                if (view_id == 1u) {
                    look_process_view(view_id, cam);
                }
                if (dd >= 30.0f && (view_id >= 1u && view_id <= 5u)) {
                    max_far = (view_id == 4u) ? 70.0f : ((view_id == 3u) ? (dd * 0.75f) : dd);
                    if (view_id == 3u && max_far < 35.0f) max_far = 35.0f;
                    if (cam && rh_mem_committed(cam + 0xC0u, 4)) {
                        if (MRF32(cam + 0xC0u) > max_far) {
                            MRF32(cam + 0xC0u) = max_far;
                        }
                    }
                }
            }
        }
    }

    if (s_orig_5c7ec0) {
        s_orig_5c7ec0();
    }

    if (max_far >= 30.0f && cam && rh_mem_committed(cam + 0xC0u, 4)) {
        if (MRF32(cam + 0xC0u) > max_far) {
            MRF32(cam + 0xC0u) = max_far;
        }
    }

    if (view_id == 1u && g_p_mem_base && *g_p_mem_base) {
        static uint32_t s_last_pacing_fc = 0;
        static int s_crowd_checked = 0;
        static int s_allow_crowd = 0;
        if (!s_crowd_checked) {
            s_crowd_checked = 1;
            const char *env_crowd = getenv("NFSU2_CROWD");
            s_allow_crowd = (env_crowd && (strcmp(env_crowd, "1") == 0 || strcasecmp(env_crowd, "on") == 0));
            nfs_rust_set_crowd_suppression(!s_allow_crowd);
        }
        uint32_t fc = rh_mem_committed(0x00870818u, 4) ? MR32(0x00870818u) : 0;
        if (fc != s_last_pacing_fc) {
            s_last_pacing_fc = fc;
            if (!s_orig_dispatch_d3d9) g_frame_count++;
            sndlead_frame_tick();
            if (!s_allow_crowd && rh_mem_committed(0x00870cd0u, 4) && MR32(0x00870cd0u) != 0) {
                MR32(0x00870cd0u) = 0;
            }
            if (rh_mem_committed(0x00870cc4u, 4) && MR32(0x00870cc4u) > 1u) {
                MR32(0x00870cc4u) = 1u;
            }
            if (rh_mem_committed(0x00870cfcu, 4) && MR32(0x00870cfcu) != 0) {
                MR32(0x00870cfcu) = 0;
            }
            if (rh_mem_committed(0x007fb700u, 4)) MR32(0x007fb700u) = 1u;
            if (rh_mem_committed(0x007fb714u, 4)) MRF32(0x007fb714u) = 6.0f;
        }
    }
}

static void hook_sub_005caad0(void) {
    static int s_gc_checked = 0;
    static void *s_h_gc = NULL;
    static void (*s_fn_bypass)(int) = NULL;
    static void (*s_set_contrast_active)(uint32_t) = NULL;

    if (!s_gc_checked) {
        s_gc_checked = 1;
        s_h_gc = dlopen("libgraphics_cache.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (!s_h_gc) s_h_gc = dlopen("libgraphics_cache.so", RTLD_LAZY | RTLD_GLOBAL);
        if (s_h_gc) {
            s_fn_bypass = (void (*)(int))dlsym(s_h_gc, "nfs_camera_cull_set_bypass");
            s_set_contrast_active = (void (*)(uint32_t))dlsym(s_h_gc, "nfs_contrast_set_active");
        }
        if (!s_set_contrast_active) {
            void *h_core = dlopen("libgame_core.so", RTLD_NOLOAD | RTLD_GLOBAL);
            if (!h_core) h_core = dlopen("libgame_core.so", RTLD_LAZY | RTLD_GLOBAL);
            if (h_core) s_set_contrast_active = (void (*)(uint32_t))dlsym(h_core, "nfs_contrast_set_active");
        }
    }

    if (s_fn_bypass) s_fn_bypass(1);

    if (g_p_mem_base && *g_p_mem_base) {
        uint32_t view4 = 0x00832DE0u + 4u * 0x70u; // 0x00832FA0
        if (rh_mem_committed(view4 + 0x40u, 4)) {
            uint32_t cam4 = MR32(view4 + 0x40u);
            if (cam4 >= 0x10000u && rh_mem_committed(cam4 + 0xC0u, 4)) {
                // Bound road reflection camera FarZ to 70m max.
                // Setting 10,000m causes the entire city geometry to be drawn twice
                // on wet/puddle world sections, overflowing Mali Valhall TBDR tile buffers.
                float refl_far = 70.0f;
                if (MRF32(cam4 + 0xC0u) > refl_far || MRF32(cam4 + 0xC0u) < 10.0f) {
                    MRF32(cam4 + 0xC0u) = refl_far;
                }
            }
        }
    }

    int do_bypass = 1;

    uint32_t user_contrast = 0;
    if (do_bypass) {
        uint32_t master_contrast = 0;
        if (rh_mem_committed(0x00876518u, 4)) {
            master_contrast = MR32(0x00876518u);
        }
        if (rh_mem_committed(0x00870D00u, 4)) {
            user_contrast = MR32(0x00870D00u) | master_contrast;
            if (s_set_contrast_active) {
                s_set_contrast_active(user_contrast);
            }
            // Clamping 0x00870D00 to 0 inside scene render causes SPEED2.EXE to cleanly
            // skip the slow 3-pass ping-pong StretchRect resolve without hooking internal functions.
            MR32(0x00870D00u) = 0;
        }
        if (rh_mem_committed(0x00870CFCu, 4)) {
            MR32(0x00870CFCu) = 0;
        }
        if (rh_mem_committed(0x00876514u, 4)) {
            MR32(0x00876514u) = 0;
        }
    }

    if (s_orig_5caad0) {
        s_orig_5caad0();
    }

    if (do_bypass) {
        // Keep engine post-processing flags clamped to 0 so SPEED2.EXE never executes
        // the heavy 3-pass ping-pong StretchRect resolve that causes the 40 FPS drop on mobile TBDR GPUs.
        if (rh_mem_committed(0x00870D00u, 4)) {
            MR32(0x00870D00u) = 0;
        }
        if (rh_mem_committed(0x00870CFCu, 4)) {
            MR32(0x00870CFCu) = 0;
        }
    }

    if (s_fn_bypass) s_fn_bypass(0);
}

/* =========================================================================
 * GameCore Enhancements (nfsu2-web parity: HFR, AI Catchup, Audio & Particles)
 * ========================================================================= */

#define G_CAREER        0x00863470u
#define CAREER_RACE     0x8730u
#define DRV_PACE        0x770u            /* AI driver: M, the target speed's factor */
#define DRV_VEHICLE     0x4u
#define VEH_BOOST       0x468u            /* vehicle: max(M, 1), power and grip */

static void catchup_apply(uint32_t drv) {
    if (!rh_mem_committed(G_CAREER + CAREER_RACE, 4) || !MR32(G_CAREER + CAREER_RACE)) return;
    if (!drv || !rh_mem_committed(drv + DRV_PACE, 4) || !rh_mem_committed(drv + DRV_VEHICLE, 4)) return;
    uint32_t veh = MR32(drv + DRV_VEHICLE);
    int car = (veh && rh_mem_committed(veh + VEH_BOOST, 4));
    float m = MRF32(drv + DRV_PACE);
    float b = car ? MRF32(veh + VEH_BOOST) : 1.0f;
    // Normalize catchup: clamp AI pace factor and vehicle boost factor to 1.0 max
    // prevents artificial rocket speed spikes when AI is behind player
    if (m > 1.0f) {
        MRF32(drv + DRV_PACE) = 1.0f;
    }
    if (car && b > 1.0f) {
        MRF32(veh + VEH_BOOST) = 1.0f;
    }
}

static void hook_sub_004172d0(void) {
    uint32_t drv = (g_p_ecx && g_p_mem_base && *g_p_mem_base) ? *g_p_ecx : 0;
    if (s_orig_4172d0) {
        s_orig_4172d0();
    }
    if (drv) {
        catchup_apply(drv);
    }
}

#define A_CAP 0x007a572cu
static void hook_sub_005ff490(void) {
    float cap = (rh_mem_committed(A_CAP, 4)) ? MRF32(A_CAP) : 0.0f;
    if (s_orig_5ff490) {
        s_orig_5ff490();
    }
    // Restore frame rate cap request to prevent network/career races from locking to 30 FPS
    if (rh_mem_committed(A_CAP, 4)) {
        MRF32(A_CAP) = cap;
    }
}

#define A_REALDT 0x00865198u
static void hook_sub_00492e10(void) {
    uint32_t esp = (g_p_esp && g_p_mem_base && *g_p_mem_base) ? *g_p_esp : 0;
    uint32_t ecx = (g_p_ecx) ? *g_p_ecx : 0;
    if (esp >= 0x10000u && rh_mem_committed(esp, 8) && ecx == 0x00835860u) {
        if (MR32(esp) == 0x005d2c1cu) {
            float k = MRF32(esp + 4u);
            if (rh_mem_committed(A_REALDT, 4)) {
                float dt = MRF32(A_REALDT);
                if (dt > 0.0f && dt < k) {
                    MRF32(esp + 4u) = dt;
                }
            }
        }
    }
    if (s_orig_492e10) {
        s_orig_492e10();
    }
}

#define A_FRAMECNT   0x00870818u
#define A_STREAK_FC  0x008a44ecu
#define G_WORLD      0x008a4454u
#define TR_CARS 16
#define TR_HIST 24
#define TR_GAP  0.1

typedef struct {
    uint32_t ring;
    int n, head;
    double t[TR_HIST];
    uint8_t m[TR_HIST][64];
    uint8_t p[TR_HIST][16];
} TrailHist;

static TrailHist g_tr[TR_CARS];
static double g_tr_time = 0.0;

static TrailHist *trail_of(uint32_t ring) {
    int free_i = -1;
    for (int i = 0; i < TR_CARS; i++) {
        if (g_tr[i].ring == ring) return &g_tr[i];
        if (!g_tr[i].ring && free_i < 0) free_i = i;
    }
    if (free_i < 0) free_i = (int)(ring >> 4) % TR_CARS;
    memset(&g_tr[free_i], 0, sizeof(g_tr[free_i]));
    g_tr[free_i].ring = ring;
    return &g_tr[free_i];
}

static int trail_pick(const TrailHist *h, double t) {
    for (int k = 0; k < h->n; k++) {
        int i = (h->head - k + TR_HIST) % TR_HIST;
        if (h->t[i] <= t + 1e-4) return i;
    }
    return -1;
}

static void hook_sub_0061ad80(void) {
    if (!rh_mem_committed(A_FRAMECNT, 4) || !rh_mem_committed(A_STREAK_FC, 4) || !rh_mem_committed(G_WORLD, 4)) {
        if (s_orig_61ad80) s_orig_61ad80();
        return;
    }
    if (MR32(A_FRAMECNT) == MR32(A_STREAK_FC)) {
        if (s_orig_61ad80) s_orig_61ad80();
        return;
    }
    float dt = rh_mem_committed(A_REALDT, 4) ? MRF32(A_REALDT) : 0.0166667f;
    g_tr_time += (dt > 0.0f && dt < 0.25f) ? dt : 0.0;
    uint32_t rings[TR_CARS];
    int idx0[TR_CARS], nc = 0;
    uint32_t head = MR32(G_WORLD);
    if (head && rh_mem_committed(head, 4)) {
        for (uint32_t e = MR32(head); e && e != head && nc < TR_CARS && rh_mem_committed(e + 0x554, 4); e = MR32(e)) {
            uint32_t r = MR32(e + 0x554);
            if (!r || !rh_mem_committed(r + 0x2180, 4)) continue;
            rings[nc] = r;
            idx0[nc] = (int)MR32(r + 0x2180);
            nc++;
        }
    }
    if (s_orig_61ad80) {
        s_orig_61ad80();
    }
    double back = 1.0 / 60.0;
    for (int i = 0; i < nc; i++) {
        uint32_t r = rings[i];
        if (!rh_mem_committed(r + 0x2180, 4)) continue;
        int idx = (int)MR32(r + 0x2180);
        if (idx == idx0[i] || idx < 0 || idx > 2) continue;
        TrailHist *h = trail_of(r);
        if (h->n && (g_tr_time - h->t[h->head] > TR_GAP)) h->n = 0;
        h->head = (h->head + 1) % TR_HIST;
        if (h->n < TR_HIST) h->n++;
        h->t[h->head] = g_tr_time;
        if (rh_mem_committed(r + 0x2090 + (uint32_t)idx * 64u, 64)) {
            memcpy(h->m[h->head], (void *)GPTR(r + 0x2090 + (uint32_t)idx * 64u), 64);
        }
        if (rh_mem_committed(r + 0x2150 + (uint32_t)idx * 16u, 16)) {
            memcpy(h->p[h->head], (void *)GPTR(r + 0x2150 + (uint32_t)idx * 16u), 16);
        }
        int prev = (idx + 2) % 3, old = (idx + 1) % 3;
        int a = trail_pick(h, g_tr_time - back);
        if (a < 0) continue;
        int b = trail_pick(h, h->t[a] - back);
        if (rh_mem_committed(r + 0x2090 + (uint32_t)prev * 64u, 64)) {
            memcpy((void *)GPTR(r + 0x2090 + (uint32_t)prev * 64u), h->m[a], 64);
        }
        if (rh_mem_committed(r + 0x2150 + (uint32_t)prev * 16u, 16)) {
            memcpy((void *)GPTR(r + 0x2150 + (uint32_t)prev * 16u), h->p[a], 16);
        }
        if (b >= 0) {
            if (rh_mem_committed(r + 0x2090 + (uint32_t)old * 64u, 64)) {
                memcpy((void *)GPTR(r + 0x2090 + (uint32_t)old * 64u), h->m[b], 64);
            }
            if (rh_mem_committed(r + 0x2150 + (uint32_t)old * 16u, 16)) {
                memcpy((void *)GPTR(r + 0x2150 + (uint32_t)old * 16u), h->p[b], 16);
            }
        }
    }
}

#define A_SND_ON     0x00787a74u
#define A_SND_PAUSE  0x00787a78u
static uint32_t g_au_this = 0;
static double   g_au_carry = 0.0;

static void hook_sub_00483b40(void) {
    uint32_t self = (g_p_ecx) ? *g_p_ecx : 0;
    uint32_t esp = (g_p_esp && g_p_mem_base && *g_p_mem_base) ? *g_p_esp : 0;
    float dt = (esp >= 0x10000u && rh_mem_committed(esp + 4u, 4)) ? MRF32(esp + 4u) : 0.0f;
    if (self != g_au_this) {
        g_au_this = self;
        g_au_carry = 0.0;
    }
    int ok = self && rh_mem_committed(self + 0xa7, 1) &&
             rh_mem_committed(A_SND_ON, 4) && MR32(A_SND_ON) &&
             rh_mem_committed(A_SND_PAUSE, 4) && !MR32(A_SND_PAUSE);
    if (!ok || dt <= 0.0f) {
        if (dt > 0.0f) g_au_carry = 0.0;
        if (s_orig_483b40) s_orig_483b40();
        return;
    }
    int active = rh_mem_committed(self + 0x13, 1) ? !MR8(self + 0x13) : 1;
    uint32_t stamp = rh_mem_committed(self + 0xa4, 4) ? MR32(self + 0xa4) : 0;
    double d2 = dt;
    if (active) {
        d2 = (double)dt + g_au_carry;
        if (d2 > 0.25) d2 = 0.25;
        MRF32(esp + 4u) = (float)d2;
    }
    if (s_orig_483b40) {
        s_orig_483b40();
    }
    if (rh_mem_committed(self + 0xa4, 4) && MR32(self + 0xa4) != stamp) {
        g_au_carry = 0.0;
    } else {
        g_au_carry = active ? d2 : 0.0;
    }
}

#define A_FE_SPIN     0x007a667cu
static void fe_spinners_tick(void) {
    if (!rh_mem_committed(A_FE_SPIN, 4) || !rh_mem_committed(A_REALDT, 4)) return;
    static float s_van_spin = -1.0f;
    if (s_van_spin < 0.0f) s_van_spin = MRF32(A_FE_SPIN);
    float dt = MRF32(A_REALDT);
    if (dt > 0.0f && dt < 0.25f) {
        MRF32(A_FE_SPIN) = s_van_spin * 60.0f * dt;
    } else {
        MRF32(A_FE_SPIN) = s_van_spin;
    }
}

static void hook_recomp_hook(uint32_t va) {
    if (va == 0x005C7F64u) { // FixFOV (FOV_SCALE)
        uint32_t ebp = (g_p_ebp) ? *g_p_ebp : 0;
        uint32_t ecx = (g_p_ecx) ? *g_p_ecx : 0;
        uint32_t view = (ebp >= 0x10000u && ebp <= 0x7FFFF000u && rh_mem_committed(ebp + 4u, 4)) ? MR32(ebp + 4u) : ecx;
        if (view == 0) view = ecx;

        if (s_orig_recomp_hook) {
            s_orig_recomp_hook(va);
        }

        float dd = get_configured_draw_distance();
        if (dd >= 30.0f && (view >= 1u && view <= 5u)) {
            float max_far = (view == 4u) ? 70.0f : ((view == 3u) ? (dd * 0.75f) : dd);
            if (view == 3u && max_far < 35.0f) max_far = 35.0f;
            uint32_t esp = (g_p_esp && *g_p_esp) ? *g_p_esp : 0;
            if (esp >= 0x10000u && rh_mem_committed(esp + 0x0Cu, 4)) {
                if (MRF32(esp + 0x0Cu) > max_far) {
                    MRF32(esp + 0x0Cu) = max_far;
                }
            }
            uint32_t cam = (ebp >= 0x10000u && ebp <= 0x7FFFF000u && rh_mem_committed(ebp + 0x40u, 4)) ? MR32(ebp + 0x40u) : 0;
            if (cam >= 0x10000u && rh_mem_committed(cam + 0xC0u, 4)) {
                if (MRF32(cam + 0xC0u) > max_far) {
                    MRF32(cam + 0xC0u) = max_far;
                }
            }
        }
        return;
    }

    if (s_orig_recomp_hook) {
        s_orig_recomp_hook(va);
    }
}

static void look_camera_frame_tick(void) {
    restore_turned_cameras();
}

typedef struct {
    int d3d9_state_filter;
    int d3d9_ref_folding;
    int car_envmap_throttle; /* 0=Original, 1=Half, 2=Quarter, 3=Off */
    int fps_limit;           /* 30, 60, 90, 120, 0(Uncapped) */
    int dxvk_invariant_pos;  /* 1=Safe(True), 0=Fast(False) */
    int boot_invariant_pos;
    int d3d9_filter_log;
    int loaded;
} AdvOptionsState;

static AdvOptionsState s_adv_cfg = { 1, 1, 0, 60, 1, 1, 1, 0 };
static _Atomic int s_d3d9_ref_flush_requested = 0;
static _Atomic int s_d3d9_state_forget_requested = 0;
static uint32_t s_saved_car_refl_rate = 0;
static int s_car_envmap_applied = 0;

static void adv_options_cfg_path(char *out, size_t out_sz) {
    const char *dir = getenv("NFSU2_DATA_DIR");
    if (!dir || !*dir) dir = "/sdcard/nfsu2";
    snprintf(out, out_sz, "%s/extra_settings.cfg", dir);
}

static void adv_options_load(void) {
    if (s_adv_cfg.loaded) return;
    s_adv_cfg.loaded = 1;

    const char *env_fps = getenv("NFSU2_FPS");
    if (env_fps && *env_fps) {
        int f = atoi(env_fps);
        if (f == 0 || f == 30 || f == 60 || f == 90 || f == 120) {
            s_adv_cfg.fps_limit = f;
        }
    } else if (s_target_fps > 0) {
        s_adv_cfg.fps_limit = (int)s_target_fps;
    }

    char path[512];
    adv_options_cfg_path(path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) {
        s_adv_cfg.boot_invariant_pos = s_adv_cfg.dxvk_invariant_pos;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        const char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        int iv = atoi(v);
        if (strncmp(k, "d3d9_state_filter", 17) == 0) {
            s_adv_cfg.d3d9_state_filter = iv ? 1 : 0;
        } else if (strncmp(k, "d3d9_ref_folding", 16) == 0) {
            s_adv_cfg.d3d9_ref_folding = iv ? 1 : 0;
        } else if (strncmp(k, "car_envmap_throttle", 19) == 0) {
            if (iv >= 0 && iv <= 3) s_adv_cfg.car_envmap_throttle = iv;
        } else if (strncmp(k, "invariant_position", 18) == 0) {
            s_adv_cfg.dxvk_invariant_pos = iv ? 1 : 0;
        } else if (strncmp(k, "d3d9_filter_log", 15) == 0) {
            s_adv_cfg.d3d9_filter_log = iv ? 1 : 0;
        }
    }
    fclose(fp);
    s_adv_cfg.boot_invariant_pos = s_adv_cfg.dxvk_invariant_pos;
}

static void adv_update_dxvk_conf_file(const char *conf_path, int invariant_pos) {
    if (!conf_path || !*conf_path) return;
    FILE *fp = fopen(conf_path, "r");
    if (!fp) return;

    char buf[8192];
    size_t out_len = 0;
    int replaced = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp) && out_len + 260 < sizeof(buf)) {
        if (strncmp(line, "d3d9.invariantPosition", 22) == 0) {
            int n = snprintf(buf + out_len, sizeof(buf) - out_len,
                "d3d9.invariantPosition = %s\n", invariant_pos ? "True" : "False");
            if (n > 0) out_len += (size_t)n;
            replaced = 1;
        } else {
            size_t len = strlen(line);
            memcpy(buf + out_len, line, len);
            out_len += len;
        }
    }
    fclose(fp);

    if (!replaced && out_len + 48 < sizeof(buf)) {
        int n = snprintf(buf + out_len, sizeof(buf) - out_len,
            "d3d9.invariantPosition = %s\n", invariant_pos ? "True" : "False");
        if (n > 0) out_len += (size_t)n;
    }

    FILE *wf = fopen(conf_path, "w");
    if (wf) {
        fwrite(buf, 1, out_len, wf);
        fclose(wf);
    }
}

static void adv_options_save(void) {
    char path[512];
    adv_options_cfg_path(path, sizeof(path));
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp,
            "d3d9_state_filter=%d\n"
            "d3d9_ref_folding=%d\n"
            "car_envmap_throttle=%d\n"
            "invariant_position=%d\n"
            "d3d9_filter_log=%d\n",
            s_adv_cfg.d3d9_state_filter,
            s_adv_cfg.d3d9_ref_folding,
            s_adv_cfg.car_envmap_throttle,
            s_adv_cfg.dxvk_invariant_pos,
            s_adv_cfg.d3d9_filter_log);
        fclose(fp);
    }

    const char *dxvk_env = getenv("DXVK_CONFIG_FILE");
    if (dxvk_env && *dxvk_env) {
        adv_update_dxvk_conf_file(dxvk_env, s_adv_cfg.dxvk_invariant_pos);
    }
    const char *dir = getenv("NFSU2_DATA_DIR");
    if (!dir || !*dir) dir = "/sdcard/nfsu2";
    char dxvk_game[512];
    snprintf(dxvk_game, sizeof(dxvk_game), "%s/dxvk.conf", dir);
    adv_update_dxvk_conf_file(dxvk_game, s_adv_cfg.dxvk_invariant_pos);
}

static void adv_apply_car_envmap(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    if (!rh_mem_committed(0x007fee90u, 16) || !rh_mem_committed(0x00870cb8u, 4)) return;

    int mode = s_adv_cfg.car_envmap_throttle;
    if (mode == 3) {
        uint32_t cur = MR32(0x00870cb8u);
        if (cur != 0u) s_saved_car_refl_rate = cur;
        MR32(0x00870cb8u) = 0u;
    } else {
        if (MR32(0x00870cb8u) == 0u && s_saved_car_refl_rate != 0u) {
            MR32(0x00870cb8u) = s_saved_car_refl_rate;
        }
        uint32_t mult = (mode == 1) ? 2u : ((mode == 2) ? 4u : 1u);
        MR32(0x007fee94u) = 6u * mult;
        MR32(0x007fee98u) = 4u * mult;
        MR32(0x007fee9cu) = 1u * mult;
    }
    s_car_envmap_applied = 1;
}

static void adv_apply_fps_limit(int fps) {
    s_adv_cfg.fps_limit = fps;
    char fps_buf[16];
    snprintf(fps_buf, sizeof(fps_buf), "%d", fps);
    setenv("NFSU2_FPS", fps_buf, 1);
    nfs_roam_set_frame_limit((uint32_t)fps);
    if (!s_fn_set_guest_frame_limit) {
        void *h_main = dlopen("libnfsu2_android.so", RTLD_NOLOAD | RTLD_GLOBAL);
        s_fn_set_guest_frame_limit = (set_guest_frame_limit_fn)dlsym(
            h_main ? h_main : RTLD_DEFAULT, "_Z18setGuestFrameLimitj");
    }
    if (s_fn_set_guest_frame_limit) {
        s_fn_set_guest_frame_limit((uint32_t)fps);
    }
}

#define D3D9_REF_SLOTS 256u

typedef struct {
    uint32_t handle;
    uint32_t token_addref;
    uint32_t base_refs;
    uint32_t extra_refs;
    uint8_t  kind;
} D3D9RefEntry;

typedef struct {
    uint32_t device_handle;
    uint8_t  in_state_block;
    uint64_t epoch;
    uint32_t rs[256];
    uint8_t  rs_valid[256];
    uint32_t ss[16][16];
    uint8_t  ss_valid[16][16];
    uint32_t tss[8][33];
    uint8_t  tss_valid[8][33];
    uint32_t tex[16];
    uint64_t tex_epoch[16];
    uint8_t  tex_valid[16];
    uint32_t decl;
    uint64_t decl_epoch;
    uint8_t  decl_valid;
    uint32_t fvf;
    uint8_t  fvf_valid;
    uint32_t vs;
    uint64_t vs_epoch;
    uint8_t  vs_valid;
    uint32_t ps;
    uint64_t ps_epoch;
    uint8_t  ps_valid;
    uint32_t stream_buf[4];
    uint32_t stream_off[4];
    uint32_t stream_stride[4];
    uint64_t stream_epoch[4];
    uint8_t  stream_valid[4];
    uint32_t stream_freq[4];
    uint8_t  stream_freq_valid[4];
    uint32_t ib;
    uint64_t ib_epoch;
    uint8_t  ib_valid;
    uint32_t light_en[8];
    uint8_t  light_valid[8];
    uint32_t sw_vp;
    uint8_t  sw_vp_valid;
    uint32_t tf[264][16];
    uint8_t  tf_valid[264];
    uint32_t vp[6];
    uint8_t  vp_valid;
    uint32_t vs_c[256 * 4];
    uint8_t  vs_c_valid[256];
    uint32_t ps_c[224 * 4];
    uint8_t  ps_c_valid[224];
    D3D9RefEntry refs[D3D9_REF_SLOTS];
    uint64_t total_calls;
    uint64_t filtered_calls;
    uint64_t ref_folded_calls;
    uint64_t present_frames;
} D3D9ShadowCache;

static D3D9ShadowCache s_d3d9_cache;

static void d3d9_cache_forget(void) {
    memset(s_d3d9_cache.rs_valid, 0, sizeof(s_d3d9_cache.rs_valid));
    memset(s_d3d9_cache.ss_valid, 0, sizeof(s_d3d9_cache.ss_valid));
    memset(s_d3d9_cache.tss_valid, 0, sizeof(s_d3d9_cache.tss_valid));
    memset(s_d3d9_cache.tex_valid, 0, sizeof(s_d3d9_cache.tex_valid));
    memset(s_d3d9_cache.stream_valid, 0, sizeof(s_d3d9_cache.stream_valid));
    memset(s_d3d9_cache.stream_freq_valid, 0, sizeof(s_d3d9_cache.stream_freq_valid));
    memset(s_d3d9_cache.light_valid, 0, sizeof(s_d3d9_cache.light_valid));
    memset(s_d3d9_cache.tf_valid, 0, sizeof(s_d3d9_cache.tf_valid));
    memset(s_d3d9_cache.vs_c_valid, 0, sizeof(s_d3d9_cache.vs_c_valid));
    memset(s_d3d9_cache.ps_c_valid, 0, sizeof(s_d3d9_cache.ps_c_valid));
    s_d3d9_cache.decl_valid = 0;
    s_d3d9_cache.fvf_valid = 0;
    s_d3d9_cache.vs_valid = 0;
    s_d3d9_cache.ps_valid = 0;
    s_d3d9_cache.ib_valid = 0;
    s_d3d9_cache.sw_vp_valid = 0;
    s_d3d9_cache.vp_valid = 0;
}

static inline int d3d9_is_foldable_kind(uint32_t kind) {
    return (kind == 3u) || (kind >= 6u && kind <= 10u);
}

static inline uint32_t d3d9_ref_slot_hash(uint32_t handle) {
    return ((handle >> 2) * 2654435761u) & (D3D9_REF_SLOTS - 1u);
}

static void d3d9_ref_flush_entry(D3D9RefEntry *e) {
    if (!e || e->handle == 0u || e->extra_refs == 0u || !s_orig_dispatch_d3d9) return;
    uint32_t h = e->handle;
    while (e->extra_refs > 0u) {
        uint32_t ac = 0u;
        e->base_refs = s_orig_dispatch_d3d9(e->token_addref, &h, &ac);
        e->extra_refs--;
    }
}

static void d3d9_ref_flush_all(void) {
    for (uint32_t i = 0; i < D3D9_REF_SLOTS; i++) {
        if (s_d3d9_cache.refs[i].handle != 0u && s_d3d9_cache.refs[i].extra_refs > 0u) {
            d3d9_ref_flush_entry(&s_d3d9_cache.refs[i]);
        }
    }
    memset(s_d3d9_cache.refs, 0, sizeof(s_d3d9_cache.refs));
}

static D3D9RefEntry *d3d9_ref_lookup(uint32_t handle, uint8_t kind) {
    if (handle == 0u) return NULL;
    uint32_t h = d3d9_ref_slot_hash(handle);
    for (uint32_t step = 0; step < 8u; step++) {
        D3D9RefEntry *e = &s_d3d9_cache.refs[(h + step) & (D3D9_REF_SLOTS - 1u)];
        if (e->handle == handle && e->kind == kind) return e;
        if (e->handle == 0u) return NULL;
    }
    return NULL;
}

static D3D9RefEntry *d3d9_ref_alloc(uint32_t handle, uint8_t kind) {
    if (handle == 0u) return NULL;
    uint32_t h = d3d9_ref_slot_hash(handle);
    D3D9RefEntry *empty = NULL;
    for (uint32_t step = 0; step < 8u; step++) {
        D3D9RefEntry *e = &s_d3d9_cache.refs[(h + step) & (D3D9_REF_SLOTS - 1u)];
        if (e->handle == handle && e->kind == kind) return e;
        if (e->handle == 0u && !empty) empty = e;
    }
    if (empty) return empty;
    D3D9RefEntry *victim = &s_d3d9_cache.refs[h];
    d3d9_ref_flush_entry(victim);
    victim->handle = 0u;
    victim->extra_refs = 0u;
    return victim;
}

static inline const void *d3d9_guest_ptr(uint32_t va, uint32_t len) {
    if (!g_p_mem_base || !*g_p_mem_base) return NULL;
    if (va < 0x00010000u || (uint64_t)va + (uint64_t)len > 0x80000000ULL) return NULL;
    return (const void *)((uintptr_t)(*g_p_mem_base) + (uintptr_t)va);
}

static inline void d3d9_cache_sync_device(uint32_t dev) {
    if (s_d3d9_cache.device_handle != dev) {
        d3d9_cache_forget();
        s_d3d9_cache.device_handle = dev;
    }
}

static uint32_t hook_dispatchGuestD3D9(uint32_t token, const uint32_t *args, uint32_t *argumentCount) {
    if (!s_orig_dispatch_d3d9) return 0;
    if (!args || !argumentCount || token < 0x7e000000u || token >= 0x7e010000u) {
        return s_orig_dispatch_d3d9(token, args, argumentCount);
    }

    if (atomic_exchange(&s_d3d9_ref_flush_requested, 0) != 0) {
        d3d9_ref_flush_all();
    }
    if (atomic_exchange(&s_d3d9_state_forget_requested, 0) != 0) {
        d3d9_cache_forget();
    }

    uint32_t off = token - 0x7e000000u;
    uint32_t kind = off >> 12;
    uint32_t method = (off >> 4) & 0xFFu;
    s_d3d9_cache.total_calls++;

    if (kind == 2u) {
        uint32_t dev = args[0];
        if (method == 17u) {
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            if (hr == 0u) {
                g_frame_count++;
                s_d3d9_cache.present_frames++;
                static void (*s_telemetry_present)(uint64_t) = NULL;
                static int s_telemetry_checked = 0;
                if (!s_telemetry_checked) {
                    s_telemetry_checked = 1;
                    void *h_gc = dlopen("libgraphics_cache.so", RTLD_NOLOAD | RTLD_GLOBAL);
                    if (!h_gc) h_gc = dlopen("libgraphics_cache.so", RTLD_LAZY | RTLD_GLOBAL);
                    if (h_gc) {
                        s_telemetry_present = (void (*)(uint64_t))dlsym(h_gc, "nfs_telemetry_record_present");
                    }
                    if (!s_telemetry_present) {
                        s_telemetry_present = (void (*)(uint64_t))dlsym(RTLD_DEFAULT, "nfs_telemetry_record_present");
                    }
                }
                if (s_telemetry_present) {
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
                    s_telemetry_present(now_ns);
                }
                if (s_adv_cfg.d3d9_filter_log &&
                    (s_d3d9_cache.present_frames % 300u) == 0u && s_d3d9_cache.total_calls > 0u) {
                    uint64_t saved = s_d3d9_cache.filtered_calls + s_d3d9_cache.ref_folded_calls;
                    double pct = (double)saved * 100.0 / (double)s_d3d9_cache.total_calls;
                    __android_log_print(ANDROID_LOG_INFO, "NFSU2-D3D9Filter",
                        "Filtered %llu state + %llu ref = %llu / %llu D3D9 calls (%.1f%%) over %llu frames",
                        (unsigned long long)s_d3d9_cache.filtered_calls,
                        (unsigned long long)s_d3d9_cache.ref_folded_calls,
                        (unsigned long long)saved,
                        (unsigned long long)s_d3d9_cache.total_calls,
                        pct,
                        (unsigned long long)s_d3d9_cache.present_frames);
                }
            }
            return hr;
        }

        /* Clamp offscreen CreateRenderTarget (28) & CreateDepthStencilSurface (29) MSAA to NONE */
        if ((method == 28u || method == 29u) && (args[4] != 0u || args[5] != 0u)) {
            uint32_t args_copy[9];
            memcpy(args_copy, args, sizeof(args_copy));
            args_copy[4] = 0u;
            args_copy[5] = 0u;
            return s_orig_dispatch_d3d9(token, args_copy, argumentCount);
        }

        if (method == 60u) {
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            if (hr == 0u) s_d3d9_cache.in_state_block = 1;
            return hr;
        }
        if (method == 61u) {
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            s_d3d9_cache.in_state_block = 0;
            d3d9_cache_forget();
            return hr;
        }
        if (method == 16u) {
            d3d9_ref_flush_all();
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            s_d3d9_cache.in_state_block = 0;
            d3d9_cache_forget();
            s_d3d9_cache.epoch++;
            return hr;
        }
        if (method == 2u) {
            d3d9_ref_flush_all();
            uint32_t refs = s_orig_dispatch_d3d9(token, args, argumentCount);
            if (refs == 0u) {
                s_d3d9_cache.in_state_block = 0;
                s_d3d9_cache.device_handle = 0;
                d3d9_cache_forget();
                s_d3d9_cache.epoch++;
            }
            return refs;
        }
        if (method == 37u || method == 34u) {
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            memset(s_d3d9_cache.tex_valid, 0, sizeof(s_d3d9_cache.tex_valid));
            s_d3d9_cache.vp_valid = 0;
            s_d3d9_cache.epoch++;
            return hr;
        }
        if (method == 39u) {
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            s_d3d9_cache.vp_valid = 0;
            return hr;
        }
        if (method == 46u) {
            uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
            if (args[1] < 264u) s_d3d9_cache.tf_valid[args[1]] = 0;
            return hr;
        }

        if (s_adv_cfg.d3d9_state_filter && !s_d3d9_cache.in_state_block && dev != 0u) {
            int same_dev = (s_d3d9_cache.device_handle == dev);
            switch (method) {
            case 57u: {
                uint32_t st = args[1], val = args[2];
                if (same_dev && st < 256u && s_d3d9_cache.rs_valid[st] && s_d3d9_cache.rs[st] == val) {
                    *argumentCount = 3u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && st < 256u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.rs[st] = val;
                    s_d3d9_cache.rs_valid[st] = 1;
                }
                return hr;
            }
            case 69u: {
                uint32_t smp = args[1], type = args[2], val = args[3];
                if (same_dev && smp < 16u && type < 16u && s_d3d9_cache.ss_valid[smp][type] && s_d3d9_cache.ss[smp][type] == val) {
                    *argumentCount = 4u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && smp < 16u && type < 16u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.ss[smp][type] = val;
                    s_d3d9_cache.ss_valid[smp][type] = 1;
                }
                return hr;
            }
            case 67u: {
                uint32_t stage = args[1], type = args[2], val = args[3];
                if (same_dev && stage < 8u && type < 33u && s_d3d9_cache.tss_valid[stage][type] && s_d3d9_cache.tss[stage][type] == val) {
                    *argumentCount = 4u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && stage < 8u && type < 33u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.tss[stage][type] = val;
                    s_d3d9_cache.tss_valid[stage][type] = 1;
                }
                return hr;
            }
            case 65u: {
                uint32_t stage = args[1], tex_h = args[2];
                if (same_dev && stage < 16u && s_d3d9_cache.tex_valid[stage] &&
                    s_d3d9_cache.tex[stage] == tex_h && s_d3d9_cache.tex_epoch[stage] == s_d3d9_cache.epoch) {
                    *argumentCount = 3u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && stage < 16u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.tex[stage] = tex_h;
                    s_d3d9_cache.tex_epoch[stage] = s_d3d9_cache.epoch;
                    s_d3d9_cache.tex_valid[stage] = 1;
                }
                return hr;
            }
            case 87u: {
                uint32_t decl_h = args[1];
                if (same_dev && s_d3d9_cache.decl_valid &&
                    s_d3d9_cache.decl == decl_h && s_d3d9_cache.decl_epoch == s_d3d9_cache.epoch) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.decl = decl_h;
                    s_d3d9_cache.decl_epoch = s_d3d9_cache.epoch;
                    s_d3d9_cache.decl_valid = 1;
                    s_d3d9_cache.fvf_valid = 0;
                }
                return hr;
            }
            case 89u: {
                uint32_t fvf_val = args[1];
                if (same_dev && s_d3d9_cache.fvf_valid && s_d3d9_cache.fvf == fvf_val) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.fvf = fvf_val;
                    s_d3d9_cache.fvf_valid = 1;
                    s_d3d9_cache.decl_valid = 0;
                }
                return hr;
            }
            case 92u: {
                uint32_t sh = args[1];
                if (same_dev && s_d3d9_cache.vs_valid &&
                    s_d3d9_cache.vs == sh && s_d3d9_cache.vs_epoch == s_d3d9_cache.epoch) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.vs = sh;
                    s_d3d9_cache.vs_epoch = s_d3d9_cache.epoch;
                    s_d3d9_cache.vs_valid = 1;
                }
                return hr;
            }
            case 107u: {
                uint32_t sh = args[1];
                if (same_dev && s_d3d9_cache.ps_valid &&
                    s_d3d9_cache.ps == sh && s_d3d9_cache.ps_epoch == s_d3d9_cache.epoch) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.ps = sh;
                    s_d3d9_cache.ps_epoch = s_d3d9_cache.epoch;
                    s_d3d9_cache.ps_valid = 1;
                }
                return hr;
            }
            case 100u: {
                uint32_t stream = args[1], buf = args[2], off_b = args[3], stride = args[4];
                if (same_dev && stream < 4u && s_d3d9_cache.stream_valid[stream] &&
                    s_d3d9_cache.stream_buf[stream] == buf &&
                    s_d3d9_cache.stream_off[stream] == off_b &&
                    s_d3d9_cache.stream_stride[stream] == stride &&
                    s_d3d9_cache.stream_epoch[stream] == s_d3d9_cache.epoch) {
                    *argumentCount = 5u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && stream < 4u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.stream_buf[stream] = buf;
                    s_d3d9_cache.stream_off[stream] = off_b;
                    s_d3d9_cache.stream_stride[stream] = stride;
                    s_d3d9_cache.stream_epoch[stream] = s_d3d9_cache.epoch;
                    s_d3d9_cache.stream_valid[stream] = 1;
                }
                return hr;
            }
            case 102u: {
                uint32_t stream = args[1], setting = args[2];
                if (same_dev && stream < 4u && s_d3d9_cache.stream_freq_valid[stream] &&
                    s_d3d9_cache.stream_freq[stream] == setting) {
                    *argumentCount = 3u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && stream < 4u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.stream_freq[stream] = setting;
                    s_d3d9_cache.stream_freq_valid[stream] = 1;
                }
                return hr;
            }
            case 104u: {
                uint32_t ib_h = args[1];
                if (same_dev && s_d3d9_cache.ib_valid &&
                    s_d3d9_cache.ib == ib_h && s_d3d9_cache.ib_epoch == s_d3d9_cache.epoch) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.ib = ib_h;
                    s_d3d9_cache.ib_epoch = s_d3d9_cache.epoch;
                    s_d3d9_cache.ib_valid = 1;
                }
                return hr;
            }
            case 53u: {
                uint32_t idx = args[1], en = args[2] ? 1u : 0u;
                if (same_dev && idx < 8u && s_d3d9_cache.light_valid[idx] && s_d3d9_cache.light_en[idx] == en) {
                    *argumentCount = 3u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && idx < 8u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.light_en[idx] = en;
                    s_d3d9_cache.light_valid[idx] = 1;
                }
                return hr;
            }
            case 77u: {
                uint32_t sw = args[1] ? 1u : 0u;
                if (same_dev && s_d3d9_cache.sw_vp_valid && s_d3d9_cache.sw_vp == sw) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u) {
                    d3d9_cache_sync_device(dev);
                    s_d3d9_cache.sw_vp = sw;
                    s_d3d9_cache.sw_vp_valid = 1;
                }
                return hr;
            }
            case 44u: {
                uint32_t st = args[1];
                const void *src = (st < 264u) ? d3d9_guest_ptr(args[2], 64u) : NULL;
                if (same_dev && src && s_d3d9_cache.tf_valid[st] &&
                    memcmp(s_d3d9_cache.tf[st], src, 64u) == 0) {
                    *argumentCount = 3u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && src) {
                    d3d9_cache_sync_device(dev);
                    memcpy(s_d3d9_cache.tf[st], src, 64u);
                    s_d3d9_cache.tf_valid[st] = 1;
                }
                return hr;
            }
            case 47u: {
                const void *src = d3d9_guest_ptr(args[1], 24u);
                if (same_dev && src && s_d3d9_cache.vp_valid &&
                    memcmp(s_d3d9_cache.vp, src, 24u) == 0) {
                    *argumentCount = 2u;
                    s_d3d9_cache.filtered_calls++;
                    return 0u;
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && src) {
                    d3d9_cache_sync_device(dev);
                    memcpy(s_d3d9_cache.vp, src, 24u);
                    s_d3d9_cache.vp_valid = 1;
                }
                return hr;
            }
            case 94u: {
                uint32_t start = args[1], cnt = args[3];
                const void *src = (start < 256u && cnt > 0u && cnt <= 256u - start)
                    ? d3d9_guest_ptr(args[2], cnt * 16u) : NULL;
                if (same_dev && src) {
                    int all_valid = 1;
                    for (uint32_t i = 0; i < cnt; i++) {
                        if (!s_d3d9_cache.vs_c_valid[start + i]) { all_valid = 0; break; }
                    }
                    if (all_valid && memcmp(&s_d3d9_cache.vs_c[start * 4u], src, (size_t)cnt * 16u) == 0) {
                        *argumentCount = 4u;
                        s_d3d9_cache.filtered_calls++;
                        return 0u;
                    }
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && src) {
                    d3d9_cache_sync_device(dev);
                    memcpy(&s_d3d9_cache.vs_c[start * 4u], src, (size_t)cnt * 16u);
                    memset(&s_d3d9_cache.vs_c_valid[start], 1, cnt);
                }
                return hr;
            }
            case 109u: {
                uint32_t start = args[1], cnt = args[3];
                const void *src = (start < 224u && cnt > 0u && cnt <= 224u - start)
                    ? d3d9_guest_ptr(args[2], cnt * 16u) : NULL;
                if (same_dev && src) {
                    int all_valid = 1;
                    for (uint32_t i = 0; i < cnt; i++) {
                        if (!s_d3d9_cache.ps_c_valid[start + i]) { all_valid = 0; break; }
                    }
                    if (all_valid && memcmp(&s_d3d9_cache.ps_c[start * 4u], src, (size_t)cnt * 16u) == 0) {
                        *argumentCount = 4u;
                        s_d3d9_cache.filtered_calls++;
                        return 0u;
                    }
                }
                uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
                if (hr == 0u && src) {
                    d3d9_cache_sync_device(dev);
                    memcpy(&s_d3d9_cache.ps_c[start * 4u], src, (size_t)cnt * 16u);
                    memset(&s_d3d9_cache.ps_c_valid[start], 1, cnt);
                }
                return hr;
            }
            default:
                break;
            }
        }
        return s_orig_dispatch_d3d9(token, args, argumentCount);
    }

    if (kind == 13u && method == 3u) {
        uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
        if (hr == 0u) {
            g_frame_count++;
            s_d3d9_cache.present_frames++;
        }
        return hr;
    }

    if (kind == 11u && method == 5u) {
        uint32_t hr = s_orig_dispatch_d3d9(token, args, argumentCount);
        d3d9_cache_forget();
        return hr;
    }

    if (kind == 1u && method == 16u) {
        d3d9_ref_flush_all();
        uint32_t args_copy[7];
        const uint32_t *call_args = args;
        if (args[4] & 0x00000004u) {
            memcpy(args_copy, args, sizeof(args_copy));
            args_copy[4] &= ~0x00000004u; /* Strip D3DCREATE_MULTITHREADED (guest is already serialized) */
            call_args = args_copy;
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-D3D9Filter",
                "Stripped D3DCREATE_MULTITHREADED from CreateDevice BehaviorFlags (0x%x -> 0x%x)",
                args[4], args_copy[4]);
        }
        uint32_t hr = s_orig_dispatch_d3d9(token, call_args, argumentCount);
        s_d3d9_cache.in_state_block = 0;
        s_d3d9_cache.device_handle = 0;
        d3d9_cache_forget();
        s_d3d9_cache.epoch++;
        return hr;
    }

    /* Paired transient AddRef (method 1) / Release (method 2) folding for non-subresource D3D9 objects */
    if (d3d9_is_foldable_kind(kind)) {
        uint32_t handle = args[0];
        if (method == 1u && handle != 0u && s_adv_cfg.d3d9_ref_folding) {
            D3D9RefEntry *e = d3d9_ref_lookup(handle, (uint8_t)kind);
            if (e && e->base_refs >= 1u && e->extra_refs < 0x7fffffffu) {
                e->extra_refs++;
                *argumentCount = 1u;
                s_d3d9_cache.ref_folded_calls++;
                return e->base_refs + e->extra_refs;
            }
            uint32_t refs = s_orig_dispatch_d3d9(token, args, argumentCount);
            if (refs >= 1u) {
                e = d3d9_ref_alloc(handle, (uint8_t)kind);
                if (e) {
                    e->handle = handle;
                    e->token_addref = token;
                    e->base_refs = refs;
                    e->extra_refs = 0u;
                    e->kind = (uint8_t)kind;
                }
            }
            return refs;
        }
        if (method == 2u && handle != 0u) {
            D3D9RefEntry *e = d3d9_ref_lookup(handle, (uint8_t)kind);
            if (e) {
                if (s_adv_cfg.d3d9_ref_folding && e->extra_refs > 0u) {
                    e->extra_refs--;
                    *argumentCount = 1u;
                    s_d3d9_cache.ref_folded_calls++;
                    return e->base_refs + e->extra_refs;
                }
                if (e->extra_refs > 0u) {
                    d3d9_ref_flush_entry(e);
                }
            }
            uint32_t refs = s_orig_dispatch_d3d9(token, args, argumentCount);
            if (refs == 0u) {
                if (e) {
                    e->handle = 0u;
                    e->extra_refs = 0u;
                }
                s_d3d9_cache.epoch++;
            } else if (e) {
                e->base_refs = refs;
            }
            return refs;
        }
    }

    if (method == 2u && ((kind >= 3u && kind <= 10u) || kind == 14u)) {
        uint32_t refs = s_orig_dispatch_d3d9(token, args, argumentCount);
        if (refs == 0u) s_d3d9_cache.epoch++;
        return refs;
    }

    return s_orig_dispatch_d3d9(token, args, argumentCount);
}

static void hook_nfs_widescreen_apply_frame_rate(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    /* Disarm SPEED2.EXE internal busy-spin frame limiter (0x865558); host FramePacer owns pacing */
    if (rh_mem_committed(0x00865558u, 4)) MRF32(0x00865558u) = 0.0f;
    if (rh_mem_committed(0x007fb710u, 8)) {
        MRF32(0x007fb710u) = 0.0f;
        MRF32(0x007fb714u) = 6.0f;
    }
    if (rh_mem_committed(0x007875bcu, 4)) MRF32(0x007875bcu) = 1.0f / 60.0f;
    if (rh_mem_committed(0x00865198u, 4) && rh_mem_committed(0x007fc858u, 4)) {
        float rdt = MRF32(0x00865198u);
        if (rdt < 1.0f / 60.0f) rdt = 1.0f / 60.0f;
        else if (rdt > 0.25f) rdt = 0.25f;
        MRF32(0x007fc858u) = rdt;
    }
}

/* =========================================================================
 * Native In-Game "Advanced Options" Menu (Bottom of Display Options)
 * ========================================================================= */
#define ADV_OPT_COUNT 6

typedef struct {
    uint32_t ptr;
    int      id;
} AdvWidgetSlot;

static uint32_t s_adv_btn_ptr = 0u;
static int      s_adv_screen_active = 0;
static uint32_t s_adv_screen_ptr = 0u;
static AdvWidgetSlot s_adv_widgets[ADV_OPT_COUNT];
static int      s_adv_widget_count = 0;
static uint32_t s_adv_str_buf = 0u;

static uint32_t adv_guest_call_direct(recomp_void_func_t target, uint32_t ecx, uint32_t edx, int n, const uint32_t *args) {
    if (!target || !g_p_mem_base || !*g_p_mem_base || !g_p_esp || *g_p_esp == 0u) return 0u;

    uint32_t saved_esp = *g_p_esp;
    uint32_t saved_eax = g_p_eax ? *g_p_eax : 0u;
    uint32_t saved_ecx = g_p_ecx ? *g_p_ecx : 0u;
    uint32_t saved_edx = g_p_edx ? *g_p_edx : 0u;
    uint32_t saved_ebx = g_p_ebx ? *g_p_ebx : 0u;
    uint32_t saved_ebp = g_p_ebp ? *g_p_ebp : 0u;
    uint32_t saved_esi = g_p_esi ? *g_p_esi : 0u;
    uint32_t saved_edi = g_p_edi ? *g_p_edi : 0u;

    for (int i = n - 1; i >= 0; i--) {
        *g_p_esp -= 4u;
        MR32(*g_p_esp) = args[i];
    }
    *g_p_esp -= 4u;
    MR32(*g_p_esp) = 0x00581475u;

    if (g_p_ecx) *g_p_ecx = ecx;
    if (g_p_edx) *g_p_edx = edx;

    target();

    uint32_t result = g_p_eax ? *g_p_eax : 0u;

    *g_p_esp = saved_esp;
    if (g_p_eax) *g_p_eax = saved_eax;
    if (g_p_ecx) *g_p_ecx = saved_ecx;
    if (g_p_edx) *g_p_edx = saved_edx;
    if (g_p_ebx) *g_p_ebx = saved_ebx;
    if (g_p_ebp) *g_p_ebp = saved_ebp;
    if (g_p_esi) *g_p_esi = saved_esi;
    if (g_p_edi) *g_p_edi = saved_edi;

    return result;
}

static uint32_t adv_guest_call(uint32_t fn_va, uint32_t ecx, uint32_t edx, int n, const uint32_t *args) {
    if (!g_p_recomp_lookup) return 0u;
    recomp_void_func_t target = (recomp_void_func_t)g_p_recomp_lookup(fn_va);
    return adv_guest_call_direct(target, ecx, edx, n, args);
}

static void adv_set_fe_str(uint32_t fe_obj, const char *text) {
    if (!fe_obj || !text || !g_p_mem_base || !*g_p_mem_base) return;
    if (s_adv_str_buf == 0u || !rh_mem_committed(s_adv_str_buf, 128)) {
        uint32_t sz = 256u;
        s_adv_str_buf = adv_guest_call(0x00575620u, 0u, 0u, 1, &sz);
    }
    if (s_adv_str_buf == 0u || !rh_mem_committed(s_adv_str_buf, 128)) return;
    char *dst = (char *)GPTR(s_adv_str_buf);
    strncpy(dst, text, 127);
    dst[127] = '\0';
    uint32_t arg = s_adv_str_buf;
    adv_guest_call(0x00535fe0u, fe_obj, 0u, 1, &arg);
}

static int adv_find_widget_id(uint32_t self) {
    if (!self) return -1;
    for (int i = 0; i < s_adv_widget_count; i++) {
        if (s_adv_widgets[i].ptr == self) return s_adv_widgets[i].id;
    }
    return -1;
}

static void adv_refresh_widget(uint32_t self, int id) {
    if (!self || !rh_mem_committed(self + 0x2cu, 8)) return;
    uint32_t lbl_obj = MR32(self + 0x2cu);
    uint32_t val_obj = MR32(self + 0x30u);

    const char *label = "";
    const char *value = "";
    switch (id) {
        case 0:
            label = "D3D9 State Filter";
            value = s_adv_cfg.d3d9_state_filter ? "On" : "Off";
            break;
        case 1:
            label = "D3D9 Ref Folding";
            value = s_adv_cfg.d3d9_ref_folding ? "On" : "Off";
            break;
        case 2:
            label = "Car Reflection Rate";
            if (s_adv_cfg.car_envmap_throttle == 0) value = "Original";
            else if (s_adv_cfg.car_envmap_throttle == 1) value = "Half";
            else if (s_adv_cfg.car_envmap_throttle == 2) value = "Quarter";
            else value = "Off";
            break;
        case 3:
            label = "FPS Limit";
            if (s_adv_cfg.fps_limit == 30) value = "30 FPS";
            else if (s_adv_cfg.fps_limit == 60) value = "60 FPS";
            else if (s_adv_cfg.fps_limit == 90) value = "90 FPS";
            else if (s_adv_cfg.fps_limit == 120) value = "120 FPS";
            else value = "Uncapped";
            break;
        case 4:
            label = "DXVK Invariant Pos";
            if (s_adv_cfg.dxvk_invariant_pos == s_adv_cfg.boot_invariant_pos) {
                value = s_adv_cfg.dxvk_invariant_pos ? "Safe (On)" : "Fast (Off)";
            } else {
                value = s_adv_cfg.dxvk_invariant_pos ? "Safe (Restart)" : "Fast (Restart)";
            }
            break;
        case 5:
            label = "D3D9 Filter Log";
            value = s_adv_cfg.d3d9_filter_log ? "On" : "Off";
            break;
        default:
            return;
    }

    if (lbl_obj) adv_set_fe_str(lbl_obj, label);
    if (val_obj) adv_set_fe_str(val_obj, value);
}

static void hook_sub_0054ee40(void) {
    uint32_t this_screen = g_p_ecx ? *g_p_ecx : 0u;
    uint32_t esp = g_p_esp ? *g_p_esp : 0u;
    uint32_t widget = (esp >= 0x10000u && rh_mem_committed(esp + 4u, 4)) ? MR32(esp + 4u) : 0u;

    /* Insert "Advanced Options" right before "Default Settings" (vtable 0x007920a0) in Display Options */
    if (this_screen && widget && rh_mem_committed(widget, 4) && MR32(widget) == 0x007920a0u) {
        s_adv_screen_active = 0;
        s_adv_widget_count = 0;
        uint32_t sz = 0x38u;
        uint32_t btn = adv_guest_call(0x00575620u, 0u, 0u, 1, &sz);
        if (btn && rh_mem_committed(btn, 0x38)) {
            uint32_t ctor_args[3] = { this_screen + 0xfcu, this_screen + 0x10cu, 1u };
            adv_guest_call(0x0051dfc0u, btn, 0u, 3, ctor_args);
            MR32(btn) = 0x00792060u; /* PCOCustomizeSettings vtable */
            s_adv_btn_ptr = btn;
            adv_guest_call_direct(s_orig_54ee40, this_screen, 0u, 1, &btn);
        }
    }

    if (s_orig_54ee40) s_orig_54ee40();
}

static void hook_sub_0049b960(void) {
    uint32_t self = g_p_ecx ? *g_p_ecx : 0u;
    if (self != 0u && self == s_adv_btn_ptr) {
        if (rh_mem_committed(self + 0x2cu, 4)) {
            uint32_t lbl = MR32(self + 0x2cu);
            if (lbl) adv_set_fe_str(lbl, "Advanced Options");
        }
        if (g_p_esp) *g_p_esp += 4u;
        return;
    }
    if (s_orig_49b960) s_orig_49b960();
}

static void hook_sub_0049b930(void) {
    uint32_t self = g_p_ecx ? *g_p_ecx : 0u;
    uint32_t esp = g_p_esp ? *g_p_esp : 0u;
    uint32_t msg = (esp >= 0x10000u && rh_mem_committed(esp + 8u, 4)) ? MR32(esp + 8u) : 0u;
    if (msg == 0x0c407210u) {
        s_adv_screen_active = (self != 0u && self == s_adv_btn_ptr) ? 1 : 0;
    }
    if (s_orig_49b930) s_orig_49b930();
}

static void hook_sub_004b6510(void) {
    if (!s_adv_screen_active) {
        s_adv_widget_count = 0;
        s_adv_screen_ptr = 0u;
        if (s_orig_4b6510) s_orig_4b6510();
        return;
    }

    uint32_t this_screen = g_p_ecx ? *g_p_ecx : 0u;
    s_adv_screen_ptr = this_screen;
    s_adv_widget_count = 0;
    adv_options_load();
    {
        const char *env_fps = getenv("NFSU2_FPS");
        if (env_fps && *env_fps) {
            int f = atoi(env_fps);
            if (f == 0 || f == 30 || f == 60 || f == 90 || f == 120) {
                s_adv_cfg.fps_limit = f;
            }
        }
    }

    if (this_screen) {
        for (int id = 0; id < ADV_OPT_COUNT; id++) {
            uint32_t sz = 0x54u;
            uint32_t w = adv_guest_call(0x00575620u, 0u, 0u, 1, &sz);
            if (!w || !rh_mem_committed(w, 0x54)) break;
            uint32_t ctor_args[5] = {
                this_screen + 0xfcu,
                this_screen + 0x10cu,
                this_screen + 0x114u,
                this_screen + 0x11cu,
                1u
            };
            adv_guest_call(0x0051e230u, w, 0u, 5, ctor_args);
            MR32(w) = 0x00792018u; /* PCOVSync vtable */
            s_adv_widgets[s_adv_widget_count].ptr = w;
            s_adv_widgets[s_adv_widget_count].id = id;
            s_adv_widget_count++;

            uint32_t add_args[2] = { w, 1u };
            adv_guest_call(0x0054ef30u, this_screen, 0u, 2, add_args);
        }

        /* Hide unused PLAYER label as in stock CustomizeDisplay::Build tail */
        uint32_t player_str = 0x00793fc0u;
        uint32_t player_hash = adv_guest_call(0x00505450u, 0u, 0u, 1, &player_str);
        uint32_t find_args[2] = { 0x0078fa8cu, player_hash };
        uint32_t player_obj = adv_guest_call(0x005379c0u, 0u, 0u, 2, find_args);
        if (player_obj) {
            adv_guest_call(0x0050ca00u, 0u, 0u, 1, &player_obj);
        }
    }

    if (g_p_esp) *g_p_esp += 4u;
}

static void hook_sub_0049b8e0(void) {
    uint32_t self = g_p_ecx ? *g_p_ecx : 0u;
    int id = adv_find_widget_id(self);
    if (id < 0) {
        if (s_orig_49b8e0) s_orig_49b8e0();
        return;
    }

    if (id == 0) {
        uint32_t hdr_args[2] = { 0x0078fa8cu, 0x42adb44cu };
        uint32_t hdr_obj = adv_guest_call(0x00537a10u, 0u, 0u, 2, hdr_args);
        if (hdr_obj) adv_set_fe_str(hdr_obj, "Advanced Options");

        if (s_adv_screen_ptr) {
            uint32_t sb_args[3] = { 8u, (uint32_t)s_adv_widget_count, 1u };
            adv_guest_call(0x00538080u, s_adv_screen_ptr + 0x6cu, 0u, 3, sb_args);
        }
    }

    adv_refresh_widget(self, id);
    if (g_p_esp) *g_p_esp += 4u;
}

static void hook_sub_0049b8a0(void) {
    uint32_t self = g_p_ecx ? *g_p_ecx : 0u;
    int id = adv_find_widget_id(self);
    if (id < 0) {
        if (s_orig_49b8a0) s_orig_49b8a0();
        return;
    }

    uint32_t esp = g_p_esp ? *g_p_esp : 0u;
    uint32_t msg = (esp >= 0x10000u && rh_mem_committed(esp + 8u, 4)) ? MR32(esp + 8u) : 0u;
    int dir = 0;
    if (msg == 0xb5971bf1u || msg == 0x0c407210u) dir = 1;       /* Right / Accept */
    else if (msg == 0x9120409eu) dir = -1;                        /* Left */

    if (dir != 0) {
        switch (id) {
            case 0:
                s_adv_cfg.d3d9_state_filter = s_adv_cfg.d3d9_state_filter ? 0 : 1;
                atomic_store(&s_d3d9_state_forget_requested, 1);
                break;
            case 1:
                s_adv_cfg.d3d9_ref_folding = s_adv_cfg.d3d9_ref_folding ? 0 : 1;
                if (!s_adv_cfg.d3d9_ref_folding) {
                    atomic_store(&s_d3d9_ref_flush_requested, 1);
                }
                break;
            case 2:
                s_adv_cfg.car_envmap_throttle = (s_adv_cfg.car_envmap_throttle + dir + 4) % 4;
                adv_apply_car_envmap();
                break;
            case 3: {
                static const int kFpsOpts[5] = { 30, 60, 90, 120, 0 };
                int idx = 1;
                for (int i = 0; i < 5; i++) {
                    if (kFpsOpts[i] == s_adv_cfg.fps_limit) { idx = i; break; }
                }
                idx = (idx + dir + 5) % 5;
                adv_apply_fps_limit(kFpsOpts[idx]);
                break;
            }
            case 4:
                s_adv_cfg.dxvk_invariant_pos = s_adv_cfg.dxvk_invariant_pos ? 0 : 1;
                break;
            case 5:
                s_adv_cfg.d3d9_filter_log = s_adv_cfg.d3d9_filter_log ? 0 : 1;
                break;
            default:
                break;
        }
        adv_options_save();
        adv_refresh_widget(self, id);
    }

    if (g_p_esp) *g_p_esp += 12u; /* ret 8 */
}

static void compute_aspect_scaled_size(uint32_t disp_w, uint32_t disp_h, uint32_t pct,
                                       uint32_t *out_w, uint32_t *out_h) {
    if (disp_w < 320u || disp_h < 240u) {
        disp_w = 1280u;
        disp_h = 720u;
    }
    uint32_t w = (uint32_t)((((uint64_t)disp_w * pct + 50u) / 100u) & ~1ULL);
    uint32_t h = (uint32_t)((((uint64_t)disp_h * pct + 50u) / 100u) & ~1ULL);
    if (h < 240u) {
        h = 240u;
        w = (uint32_t)((((uint64_t)h * disp_w + disp_h / 2u) / disp_h) & ~1ULL);
    }
    if (w < 320u) {
        w = 320u;
        h = (uint32_t)((((uint64_t)w * disp_h + disp_w / 2u) / disp_w) & ~1ULL);
    }
    if (w > 8192u) {
        w = 8192u;
        h = (uint32_t)((((uint64_t)w * disp_h + disp_w / 2u) / disp_w) & ~1ULL);
    }
    if (h > 8192u) {
        h = 8192u;
        w = (uint32_t)((((uint64_t)h * disp_w + disp_h / 2u) / disp_h) & ~1ULL);
    }
    if (w < 320u) w = 320u;
    if (h < 240u) h = 240u;
    *out_w = w & ~1u;
    *out_h = h & ~1u;
}

static void apply_custom_resolution_slots(void) {
    if (!g_p_mem_base || !*g_p_mem_base || !s_p_guest_render_modes) return;

    uint32_t disp_w = 0, disp_h = 0;
    const char *env_dw = getenv("NFSU2_DISPLAY_WIDTH");
    const char *env_dh = getenv("NFSU2_DISPLAY_HEIGHT");
    if (env_dw) disp_w = (uint32_t)atoi(env_dw);
    if (env_dh) disp_h = (uint32_t)atoi(env_dh);
    if (disp_w < 320u || disp_h < 240u) {
        disp_w = s_p_guest_render_modes[3 * 2];
        disp_h = s_p_guest_render_modes[3 * 2 + 1];
    }
    if (disp_w < 320u || disp_h < 240u) return;

    s_display_w = disp_w;
    s_display_h = disp_h;

    uint32_t init_w = s_p_guest_render_modes[5 * 2];
    uint32_t init_h = s_p_guest_render_modes[5 * 2 + 1];

    uint32_t target_pct = 0;
    const char *env_pct = getenv("NFSU2_RES_PERCENT");
    if (env_pct && *env_pct) target_pct = (uint32_t)atoi(env_pct);
    if (target_pct < 10u || target_pct > 200u) {
        target_pct = (init_w >= 320u) ? ((init_w * 100u + disp_w / 2u) / disp_w) : 75u;
    }

    uint32_t pcts[6] = { 25u, 50u, 75u, 100u, 0u, 0u };
    uint32_t count = 4u;
    int is_base = (target_pct == 25u || target_pct == 50u || target_pct == 75u || target_pct == 100u);
    if (!is_base && target_pct >= 10u && target_pct <= 200u) {
        pcts[4] = target_pct;
        count = 5u;
        for (uint32_t i = 0; i < count; i++) {
            for (uint32_t j = i + 1; j < count; j++) {
                if (pcts[j] < pcts[i]) {
                    uint32_t tmp = pcts[i];
                    pcts[i] = pcts[j];
                    pcts[j] = tmp;
                }
            }
        }
    }

    static const uint32_t label_vas[6] = {
        0x78fa1cu, 0x78fa14u, 0x78fa08u, 0x78f9fcu, 0x78f9f0u, 0x78f9e4u
    };
    static const uint32_t label_caps[6] = { 8u, 8u, 12u, 12u, 12u, 12u };

    uint32_t active_slot = 0;
    int best_diff = 999999;
    for (uint32_t i = 0; i < count; i++) {
        int diff = abs((int)pcts[i] - (int)target_pct);
        if (diff < best_diff) {
            best_diff = diff;
            active_slot = i;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t w = 0, h = 0;
        if (i == active_slot && init_w >= 320u && init_h >= 240u) {
            w = init_w;
            h = init_h;
        } else {
            compute_aspect_scaled_size(disp_w, disp_h, pcts[i], &w, &h);
        }
        s_custom_modes[i].w = w;
        s_custom_modes[i].h = h;
        s_custom_modes[i].pct = pcts[i];

        s_p_guest_render_modes[i * 2] = w;
        s_p_guest_render_modes[i * 2 + 1] = h;

        MR32(0x800538u + i * 4u) = w;
        MR32(0x800550u + i * 4u) = h;
        MR32(0x86e880u + i * 4u) = 1u;

        char buf[16];
        snprintf(buf, sizeof(buf), "%u%%", pcts[i]);
        memset(GPTR(label_vas[i]), 0, label_caps[i]);
        strncpy((char *)GPTR(label_vas[i]), buf, label_caps[i] - 1u);
    }

    for (uint32_t i = count; i < 6u; i++) {
        uint32_t fb_w = (i == 5u && init_w >= 320u) ? init_w : s_custom_modes[count - 1u].w;
        uint32_t fb_h = (i == 5u && init_h >= 240u) ? init_h : s_custom_modes[count - 1u].h;
        s_custom_modes[i].w = fb_w;
        s_custom_modes[i].h = fb_h;
        s_custom_modes[i].pct = 100u;

        s_p_guest_render_modes[i * 2] = fb_w;
        s_p_guest_render_modes[i * 2 + 1] = fb_h;

        /* Zero guest table & availability so 0x5be5b0 never exposes disabled slots (no 1280x720) */
        MR32(0x800538u + i * 4u) = 0u;
        MR32(0x800550u + i * 4u) = 0u;
        MR32(0x86e880u + i * 4u) = 0u;

        memset(GPTR(label_vas[i]), 0, label_caps[i]);
        strncpy((char *)GPTR(label_vas[i]), "100%", label_caps[i] - 1u);
    }

    s_custom_mode_count = count;
    s_res_active_slot = active_slot;
    s_custom_modes_ready = 1;

    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Res",
        "Configured %u native-aspect resolution slots (display=%ux%u active=%u [%u%% %ux%u], removed 1280x720)",
        count, disp_w, disp_h, active_slot,
        s_custom_modes[active_slot].pct,
        s_custom_modes[active_slot].w, s_custom_modes[active_slot].h);
}

static void hook_configureAndroidResolutionModes(void) {
    if (s_orig_configure_res_modes) {
        s_orig_configure_res_modes();
    }
    apply_custom_resolution_slots();
    /* connectGuestRuntime runs 7 boot ABI self-test calls (initial + slots 0..5) right after this. */
    s_boot_selftest_pending = 7;
}

static int hook_nfs_android_resolution(void) {
    if (s_boot_selftest_pending > 0) {
        s_boot_selftest_pending--;
        return s_orig_nfs_android_res ? s_orig_nfs_android_res() : 0;
    }
    if (g_p_mem_base && *g_p_mem_base) {
        if (!s_custom_modes_ready) {
            apply_custom_resolution_slots();
        }
        if (s_custom_modes_ready) {
            if (s_p_first_game_res && *s_p_first_game_res != 0u) {
                *s_p_first_game_res = 0u;
                MR32(0x870d1cu) = s_res_active_slot;
            }
            for (uint32_t i = 0; i < 6u; i++) {
                MR32(0x86e880u + i * 4u) = (i < s_custom_mode_count) ? 1u : 0u;
            }
            if (MR32(0x870d1cu) >= s_custom_mode_count) {
                MR32(0x870d1cu) = s_res_active_slot;
            }
        }
    }
    return s_orig_nfs_android_res ? s_orig_nfs_android_res() : 0;
}

static uint8_t hook_wsi_getDisplayMode(void *monitor, uint32_t mode_num, DxvkWsiMode *mode) {
    if (!s_custom_modes_ready) {
        return s_orig_wsi_get_display_mode ? s_orig_wsi_get_display_mode(monitor, mode_num, mode) : 0u;
    }
    if (monitor != (void *)1 || !mode || mode_num >= s_custom_mode_count) {
        return 0u;
    }
    mode->width = s_custom_modes[mode_num].w;
    mode->height = s_custom_modes[mode_num].h;
    mode->refresh_num = 60000u;
    mode->refresh_den = 1000u;
    mode->bits_per_pixel = 32u;
    mode->interlaced = 0u;
    return 1u;
}

static void hook_nfs_widescreen_set_resolution(unsigned width, unsigned height) {
    if (s_orig_widescreen_set_res) {
        s_orig_widescreen_set_res(width, height);
    }
    if (!s_lib_base || !g_p_mem_base || !*g_p_mem_base || !width || !height) return;
    if (s_display_w < 320u || s_display_h < 240u) return;

    uint8_t ws_enabled = *(uint8_t *)(s_lib_base + 0x97f7300);
    if (!ws_enabled) return;

    float disp_aspect = (float)s_display_w / (float)s_display_h;
    float cur_aspect = (float)width / (float)height;
    if (fabsf(cur_aspect - disp_aspect) > 0.03f) {
        float hud_scale_x = 2.0f / (disp_aspect * 480.0f);
        float hud_pos_x = disp_aspect * 240.0f;
        float hud_offset = 240.0f * disp_aspect - 320.0f;
        float dyno_x = 79.0f + ((480.0f * disp_aspect) - 640.0f) * 0.5f;

        *(float *)(s_lib_base + 0x97e64bc) = disp_aspect;
        *(float *)(s_lib_base + 0x97e64c0) = hud_pos_x;
        *(float *)(s_lib_base + 0x97f7304) = hud_offset;

        MRF32(0x0079ac10u) = hud_scale_x;
        MRF32(0x00797d50u) = hud_pos_x;
        MRF32(0x00797d58u) = hud_pos_x * 2.0f;
        MRF32(0x007fa3c0u) = dyno_x;
        MRF32(0x007fa3c4u) = 60.0f;
    }
}

static void roam_android_install_hooks(void) {
    if (atomic_load(&g_hooks_installed) || !g_p_recomp_lookup) return;
    pthread_mutex_lock(&s_hooks_mu);
    if (atomic_load(&g_hooks_installed) || !g_p_recomp_lookup) {
        pthread_mutex_unlock(&s_hooks_mu);
        return;
    }

    Dl_info dlinfo;
    if (!dladdr((void *)g_p_recomp_lookup, &dlinfo) || !dlinfo.dli_fbase) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Hook", "Failed to resolve lib base via dladdr");
        pthread_mutex_unlock(&s_hooks_mu);
        return;
    }

    uintptr_t lib_base = (uintptr_t)dlinfo.dli_fbase;
    s_lib_base = lib_base;
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 16384;

    static const uint8_t EXPECTED_4A[16] = {
        0xff, 0xc3, 0x01, 0xd1, 0xfd, 0x7b, 0x01, 0xa9,
        0xfc, 0x6f, 0x02, 0xa9, 0xfa, 0x67, 0x03, 0xa9
    };
    static const uint8_t EXPECTED_5C[16] = {
        0xff, 0x03, 0x02, 0xd1, 0xfd, 0x7b, 0x02, 0xa9,
        0xfc, 0x6f, 0x03, 0xa9, 0xfa, 0x67, 0x04, 0xa9
    };
    static const uint8_t EXPECTED_RADAR[16] = {
        0xfd, 0x7b, 0xba, 0xa9, 0xfc, 0x6f, 0x01, 0xa9,
        0xfa, 0x67, 0x02, 0xa9, 0xf8, 0x5f, 0x03, 0xa9
    };
    static const uint8_t EXPECTED_RECOMP_HOOK[16] = {
        0xff, 0x43, 0x01, 0xd1, 0xe8, 0x0b, 0x00, 0xfd,
        0xfd, 0x7b, 0x02, 0xa9, 0xf6, 0x57, 0x03, 0xa9
    };

    void *target_4a = (void *)(lib_base + 0x1464be0);
    if (memcmp(target_4a, EXPECTED_4A, 16) == 0) {
        s_orig_4a5d90 = arm64_inline_hook(target_4a, (void *)hook_sub_004a5d90);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                            "Hooked sub_004a5d90 (garage car viewer) at %p (orig: %p)",
                            target_4a, s_orig_4a5d90);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2-Hook",
                            "sub_004a5d90 prefix mismatch at %p", target_4a);
    }

    void *target_5c = (void *)(lib_base + 0x2d762b0);
    if (memcmp(target_5c, EXPECTED_5C, 16) == 0) {
        s_orig_5c7ec0 = arm64_inline_hook(target_5c, (void *)hook_sub_005c7ec0);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                            "Hooked sub_005c7ec0 (camera setup) at %p (orig: %p)",
                            target_5c, s_orig_5c7ec0);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2-Hook",
                            "sub_005c7ec0 prefix mismatch at %p", target_5c);
    }

    void *target_radar = (void *)(lib_base + 0x2754a8c);
    if (memcmp(target_radar, EXPECTED_RADAR, 16) == 0) {
        s_orig_5caad0 = arm64_inline_hook(target_radar, (void *)hook_sub_005caad0);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                            "Hooked sub_005caad0 (RenderRadar) at %p (orig: %p)",
                            target_radar, s_orig_5caad0);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2-Hook",
                            "sub_005caad0 prefix mismatch at %p", target_radar);
    }

    void *target_recomp_hook = (void *)(lib_base + 0x004ae2dc);
    if (memcmp(target_recomp_hook, EXPECTED_RECOMP_HOOK, 16) == 0) {
        s_orig_recomp_hook = (recomp_hook_func_t)arm64_inline_hook(target_recomp_hook, (void *)hook_recomp_hook);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                            "Hooked recomp_hook at %p (orig: %p)",
                            target_recomp_hook, s_orig_recomp_hook);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2-Hook",
                            "recomp_hook prefix mismatch at %p", target_recomp_hook);
    }

    // Also update k_functions dispatch table
    uintptr_t k_4a = lib_base + 0x9778160;
    if (*(uint32_t *)k_4a == 0x004a5d90) {
        if (!s_orig_4a5d90) {
            s_orig_4a5d90 = *(void **)(k_4a + 8);
        }
        uintptr_t page = k_4a & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_4a + 8) = (void *)hook_sub_004a5d90;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x004a5d90] (orig: %p)", s_orig_4a5d90);
    }

    uintptr_t k_5c = lib_base + 0x9793cd0;
    if (*(uint32_t *)k_5c == 0x005c7ec0) {
        uintptr_t page = k_5c & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_5c + 8) = (void *)hook_sub_005c7ec0;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x005c7ec0]");
    }

    uintptr_t k_radar = lib_base + 0x9793f10;
    if (*(uint32_t *)k_radar == 0x005caad0) {
        uintptr_t page = k_radar & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_radar + 8) = (void *)hook_sub_005caad0;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x005caad0]");
    }

    // 1. sub_004172d0 (AI rubberbanding catchup normalization)
    void *target_41 = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(0x004172d0u) : NULL;
    if (target_41) {
        s_orig_4172d0 = (recomp_void_func_t)arm64_inline_hook(target_41, (void *)hook_sub_004172d0);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Hooked sub_004172d0 (AI catchup) at %p (orig: %p)", target_41, s_orig_4172d0);
    }
    uintptr_t k_41 = lib_base + 0x9768b90;
    if (*(uint32_t *)k_41 == 0x004172d0) {
        uintptr_t page = k_41 & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_41 + 8) = (void *)hook_sub_004172d0;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x004172d0]");
    }

    // 2. sub_005ff490 (Online/LAN 30 FPS cap uncap)
    void *target_5f = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(0x005ff490u) : NULL;
    if (target_5f) {
        s_orig_5ff490 = (recomp_void_func_t)arm64_inline_hook(target_5f, (void *)hook_sub_005ff490);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Hooked sub_005ff490 (online 30fps cap) at %p (orig: %p)", target_5f, s_orig_5ff490);
    }
    uintptr_t k_5f = lib_base + 0x97999b0;
    if (*(uint32_t *)k_5f == 0x005ff490) {
        uintptr_t page = k_5f & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_5f + 8) = (void *)hook_sub_005ff490;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x005ff490]");
    }

    // 3. sub_00492e10 (Acid particle update dt scaling)
    void *target_49 = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(0x00492e10u) : NULL;
    if (target_49) {
        s_orig_492e10 = (recomp_void_func_t)arm64_inline_hook(target_49, (void *)hook_sub_00492e10);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Hooked sub_00492e10 (particles dt) at %p (orig: %p)", target_49, s_orig_492e10);
    }
    uintptr_t k_49 = lib_base + 0x9775a90;
    if (*(uint32_t *)k_49 == 0x00492e10) {
        uintptr_t page = k_49 & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_49 + 8) = (void *)hook_sub_00492e10;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x00492e10]");
    }

    // 4. sub_0061ad80 (NOS streaking flares trail length)
    void *target_61 = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(0x0061ad80u) : NULL;
    if (target_61) {
        s_orig_61ad80 = (recomp_void_func_t)arm64_inline_hook(target_61, (void *)hook_sub_0061ad80);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Hooked sub_0061ad80 (NOS trail) at %p (orig: %p)", target_61, s_orig_61ad80);
    }
    uintptr_t k_61 = lib_base + 0x979b8d0;
    if (*(uint32_t *)k_61 == 0x0061ad80) {
        uintptr_t page = k_61 & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_61 + 8) = (void *)hook_sub_0061ad80;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x0061ad80]");
    }

    // 5. sub_00483b40 (EAXSound::Update audio pitch carry)
    void *target_48 = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(0x00483b40u) : NULL;
    if (target_48) {
        s_orig_483b40 = (recomp_void_func_t)arm64_inline_hook(target_48, (void *)hook_sub_00483b40);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Hooked sub_00483b40 (EAX audio dt) at %p (orig: %p)", target_48, s_orig_483b40);
    }
    uintptr_t k_48 = lib_base + 0x9774540;
    if (*(uint32_t *)k_48 == 0x00483b40) {
        uintptr_t page = k_48 & ~((uintptr_t)page_sz - 1);
        mprotect((void *)page, page_sz * 2, PROT_READ | PROT_WRITE);
        *(void **)(k_48 + 8) = (void *)hook_sub_00483b40;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Updated k_functions[0x00483b40]");
    }

    // Patch GOT slot for recomp_hook so PLT jumps directly to our hook
    uintptr_t got_recomp_hook = lib_base + 0x97dd4a8;
    uintptr_t got_page = got_recomp_hook & ~((uintptr_t)page_sz - 1);
    if (mprotect((void *)got_page, page_sz * 2, PROT_READ | PROT_WRITE) == 0) {
        *(void **)got_recomp_hook = (void *)hook_recomp_hook;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Patched GOT entry for recomp_hook");
    }

    // Hook dispatchGuestD3D9 for redundant D3D9 state deduplication and Present telemetry
    void *h_main = dlopen("libnfsu2_android.so", RTLD_NOLOAD | RTLD_GLOBAL);
    void *target_dispatch = dlsym(h_main ? h_main : RTLD_DEFAULT, "_Z17dispatchGuestD3D9jPKjPj");
    if (target_dispatch && !s_orig_dispatch_d3d9) {
        void **got_dispatch = (void **)(lib_base + 0x97ddc88);
        if (*got_dispatch == target_dispatch) {
            uintptr_t gp_start = (uintptr_t)got_dispatch & ~((uintptr_t)page_sz - 1);
            uintptr_t gp_end = ((uintptr_t)(got_dispatch + 1) + (uintptr_t)page_sz - 1) & ~((uintptr_t)page_sz - 1);
            if (mprotect((void *)gp_start, gp_end - gp_start, PROT_READ | PROT_WRITE) == 0) {
                s_orig_dispatch_d3d9 = (dispatch_d3d9_func_t)target_dispatch;
                *got_dispatch = (void *)hook_dispatchGuestD3D9;
                __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                    "Hooked dispatchGuestD3D9 via GOT at %p (orig: %p)",
                    (void *)got_dispatch, target_dispatch);
            }
        }
        if (!s_orig_dispatch_d3d9) {
            static const uint8_t EXPECTED_DISPATCH_D3D9[16] = {
                0xff, 0xc3, 0x04, 0xd1, 0xfd, 0x7b, 0x0d, 0xa9,
                0xfc, 0x73, 0x00, 0xf9, 0xfa, 0x67, 0x0f, 0xa9
            };
            if (memcmp(target_dispatch, EXPECTED_DISPATCH_D3D9, 16) == 0) {
                s_orig_dispatch_d3d9 = (dispatch_d3d9_func_t)arm64_inline_hook(
                    target_dispatch, (void *)hook_dispatchGuestD3D9);
                __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                    "Hooked dispatchGuestD3D9 inline at %p (orig: %p)",
                    target_dispatch, (void *)s_orig_dispatch_d3d9);
            } else {
                __android_log_print(ANDROID_LOG_WARN, "NFSU2-Hook",
                    "dispatchGuestD3D9 prefix mismatch at %p", target_dispatch);
            }
        }
    }

    // Widen checkGuestProgress render-thread slice from 3 ms (0x002dc6c0 ns) to 16 ms (0x00f42400 ns)
    // to eliminate excessive mid-frame guest thread handovers while keeping worker slices (500 us) intact.
    uint32_t *slice_insns = (uint32_t *)(lib_base + 0x477aec);
    if (slice_insns[0] == 0x7100013fu &&
        slice_insns[1] == 0x5298d809u &&
        slice_insns[2] == 0x72a005a9u &&
        slice_insns[3] == 0xcb080268u) {
        uintptr_t sp_start = (uintptr_t)&slice_insns[1] & ~((uintptr_t)page_sz - 1);
        uintptr_t sp_end = ((uintptr_t)&slice_insns[3] + (uintptr_t)page_sz - 1) & ~((uintptr_t)page_sz - 1);
        if (mprotect((void *)sp_start, sp_end - sp_start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            slice_insns[1] = 0x52848009u; /* mov  w9, #0x2400 */
            slice_insns[2] = 0x72a01e89u; /* movk w9, #0xf4, lsl #16 -> 16,000,000 ns */
            __builtin___clear_cache((char *)&slice_insns[1], (char *)&slice_insns[3]);
            mprotect((void *)sp_start, sp_end - sp_start, PROT_READ | PROT_EXEC);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                "Patched checkGuestProgress render slice: 3 ms -> 16 ms at %p",
                (void *)&slice_insns[1]);
        }
    }

    // Reduce checkGuestProgress clock/TLS polling frequency by 16x (64 -> 1024 yields per check)
    uint32_t *yield_insns = (uint32_t *)(lib_base + 0x477a98);
    if (yield_insns[0] == 0xb9418d09u &&
        yield_insns[1] == 0x11000529u &&
        yield_insns[2] == 0x7200153fu &&
        yield_insns[3] == 0xb9018d09u) {
        uintptr_t yp_start = (uintptr_t)&yield_insns[2] & ~((uintptr_t)page_sz - 1);
        uintptr_t yp_end = ((uintptr_t)&yield_insns[3] + (uintptr_t)page_sz - 1) & ~((uintptr_t)page_sz - 1);
        if (mprotect((void *)yp_start, yp_end - yp_start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            yield_insns[2] = 0x7200253fu; /* tst w9, #0x3ff (1024 yields) */
            __builtin___clear_cache((char *)&yield_insns[2], (char *)&yield_insns[3]);
            mprotect((void *)yp_start, yp_end - yp_start, PROT_READ | PROT_EXEC);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                "Patched checkGuestProgress yield mask: 64 -> 1024 at %p",
                (void *)&yield_insns[2]);
        }
    }

    // Load persisted Advanced Options before first device/frame
    adv_options_load();

    // Verify GuestMemory::accessible zero-syscall fast path
    if (s_fn_guest_accessible && s_pp_guest_memory) {
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
            "Resolved GuestMemory::accessible=%p (memory ptr=%p) - zero-syscall mem probe active",
            (void *)s_fn_guest_accessible, (void *)s_pp_guest_memory);
    }

    // Hook resolution mode configuration, D3D9 mode enumeration, aspect-ratio lock, and frame-rate spin-limiter disarm
    s_p_guest_render_modes = (uint32_t *)dlsym(h_main ? h_main : RTLD_DEFAULT, "guestRenderModes");
    s_p_first_game_res = (uint8_t *)dlsym(h_main ? h_main : RTLD_DEFAULT, "firstGameResolution");
    s_fn_set_guest_frame_limit = (set_guest_frame_limit_fn)dlsym(h_main ? h_main : RTLD_DEFAULT, "_Z18setGuestFrameLimitj");

    struct {
        const char *sym;
        uintptr_t got_off;
        void *hook;
        void **orig_out;
    } res_got_hooks[] = {
        { "_Z31configureAndroidResolutionModesv", 0x97ddcf0u,
          (void *)hook_configureAndroidResolutionModes, (void **)&s_orig_configure_res_modes },
        { "nfs_android_resolution", 0x97dd4c0u,
          (void *)hook_nfs_android_resolution, (void **)&s_orig_nfs_android_res },
        { "_ZN4dxvk3wsi14getDisplayModeEPvjPNS0_7WsiModeE", 0x97df030u,
          (void *)hook_wsi_getDisplayMode, (void **)&s_orig_wsi_get_display_mode },
        { "nfs_widescreen_set_resolution", 0x97ddd08u,
          (void *)hook_nfs_widescreen_set_resolution, (void **)&s_orig_widescreen_set_res },
        { "nfs_widescreen_apply_frame_rate", 0x97ddc90u,
          (void *)hook_nfs_widescreen_apply_frame_rate, (void **)&s_orig_widescreen_apply_fps },
    };
    for (size_t i = 0; i < sizeof(res_got_hooks) / sizeof(res_got_hooks[0]); i++) {
        void *target = dlsym(h_main ? h_main : RTLD_DEFAULT, res_got_hooks[i].sym);
        if (!target || *res_got_hooks[i].orig_out) continue;
        void **slot = (void **)(lib_base + res_got_hooks[i].got_off);
        if (*slot == target) {
            uintptr_t gs = (uintptr_t)slot & ~((uintptr_t)page_sz - 1);
            uintptr_t ge = ((uintptr_t)(slot + 1) + (uintptr_t)page_sz - 1) & ~((uintptr_t)page_sz - 1);
            if (mprotect((void *)gs, ge - gs, PROT_READ | PROT_WRITE) == 0) {
                *res_got_hooks[i].orig_out = target;
                *slot = res_got_hooks[i].hook;
                __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                    "Hooked %s via GOT at %p (orig: %p)",
                    res_got_hooks[i].sym, (void *)slot, target);
            }
        }
    }

    // Hook Display Options & Customize Display frontend to inject "Advanced Options" right before "Default Settings"
    struct {
        const char *name;
        uint32_t guest_va;
        uintptr_t k_off;
        void *hook;
        recomp_void_func_t *orig_out;
    } adv_fe_hooks[] = {
        { "AddActionBtn",                 0x0054ee40u, 0x97916d0u, (void *)hook_sub_0054ee40, &s_orig_54ee40 },
        { "PCOCustomizeSettings::Refresh",0x0049b960u, 0x977ee20u, (void *)hook_sub_0049b960, &s_orig_49b960 },
        { "PCOCustomizeSettings::OnInput",0x0049b930u, 0x977ee10u, (void *)hook_sub_0049b930, &s_orig_49b930 },
        { "CustomizeDisplay::Build",      0x004b6510u, 0x9782990u, (void *)hook_sub_004b6510, &s_orig_4b6510 },
        { "PCOVSync::OnInput",            0x0049b8a0u, 0x977edf0u, (void *)hook_sub_0049b8a0, &s_orig_49b8a0 },
        { "PCOVSync::Refresh",            0x0049b8e0u, 0x977ee00u, (void *)hook_sub_0049b8e0, &s_orig_49b8e0 },
    };
    for (size_t i = 0; i < sizeof(adv_fe_hooks) / sizeof(adv_fe_hooks[0]); i++) {
        void *target = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(adv_fe_hooks[i].guest_va) : NULL;
        if (target && !*adv_fe_hooks[i].orig_out) {
            *adv_fe_hooks[i].orig_out = arm64_inline_hook(target, adv_fe_hooks[i].hook);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook",
                "Hooked %s (0x%08x) at %p (orig: %p)",
                adv_fe_hooks[i].name, adv_fe_hooks[i].guest_va, target, (void *)*adv_fe_hooks[i].orig_out);
        }
        uintptr_t k_entry = lib_base + adv_fe_hooks[i].k_off;
        if (*(uint32_t *)k_entry == adv_fe_hooks[i].guest_va) {
            uintptr_t kp = k_entry & ~((uintptr_t)page_sz - 1);
            if (mprotect((void *)kp, (size_t)page_sz * 2u, PROT_READ | PROT_WRITE) == 0) {
                *(void **)(k_entry + 8u) = adv_fe_hooks[i].hook;
            }
        }
    }

    atomic_store(&g_hooks_installed, 1);
    pthread_mutex_unlock(&s_hooks_mu);
}

void roam_resolution_nav(int dir) {
    /* Disabled: in-game Display Settings resolution slots are managed directly by
     * hook_configureAndroidResolutionModes / hook_nfs_android_resolution without 1280x720.
     * Mutating 0x870d1c on arbitrary menu left/right presses caused unexpected 1280x720 snaps. */
    (void)dir;
}

static void resolution_frame_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base || !s_custom_modes_ready) return;
    if (!rh_mem_committed(0x86e880u, 24)) return;
    for (uint32_t i = 0; i < 6u; i++) {
        MR32(0x86e880u + i * 4u) = (i < s_custom_mode_count) ? 1u : 0u;
    }
    if (!s_car_envmap_applied && s_adv_cfg.car_envmap_throttle != 0) {
        adv_apply_car_envmap();
    }
}

static void lobby_frame_tick(void) {
    static int s_lobby_patched = 0;
    if (s_lobby_patched || !g_p_mem_base || !*g_p_mem_base) return;
    if (rh_mem_committed(0x007fbdbcu, 16)) {
        uint8_t *p = (uint8_t *)GPTR(0x007fbdbcu);
        if (p && p[0] == '*') {
            memmove(p, p + 1, 15);
            p[15] = '\0';
            s_lobby_patched = 1;
            __android_log_print(ANDROID_LOG_INFO, "NFSU2", "roam: patched 0x007fbdbc: removed '*' from lobby host for plain TCP");
        }
    }
}

static _Atomic int s_tick_in_progress = 0;
__attribute__((visibility("default"))) void nfs_roam_frame_tick(void) {
    if (!roam_android_init_symbols() || !g_p_esp || !*g_p_esp) return;
    roam_android_protect_guest_null_page();
    if (atomic_exchange(&s_tick_in_progress, 1) != 0) return;
    pace_frame();
    static int s_perf_bound = 0;
    if (!s_perf_bound || (g_frame_count % 300) == 0) {
        s_perf_bound = 1;
        nfs_bind_to_perf_cores();
    }
    g_frame_count++;
    static uint64_t s_last_tick_ns = 0;
    struct timespec ts_tick;
    clock_gettime(CLOCK_MONOTONIC, &ts_tick);
    uint64_t now_tick_ns = (uint64_t)ts_tick.tv_sec * 1000000000ULL + (uint64_t)ts_tick.tv_nsec;
    if (s_last_tick_ns > 0 && now_tick_ns > s_last_tick_ns) {
        uint64_t tick_dur_ns = now_tick_ns - s_last_tick_ns;
        static void(*s_metrics_record_roam)(uint64_t) = NULL;
        if (!s_metrics_record_roam) s_metrics_record_roam = (void(*)(uint64_t))dlsym(RTLD_DEFAULT, "nfs_metrics_record_frame");
        if (s_metrics_record_roam) s_metrics_record_roam(tick_dur_ns);
    }
    s_last_tick_ns = now_tick_ns;
    hiddenraces_frame_tick();
    extraoptions_frame_tick();
    garage_frame_tick();
    feng_ui_expand_tick();
    menu_fixes_tick();
    sndlead_frame_tick();
    whine_frame_tick();
    framerate_tick_base_apply();
    lobby_frame_tick();
    framerate_pacing_fix();
    resolution_frame_tick();
    look_camera_frame_tick();
    fe_spinners_tick();
    static int s_gamepad_hooked_tick = 0;
    if (!s_gamepad_hooked_tick) {
        roam_apply_gamepad_layout(0);
        s_gamepad_hooked_tick = 1;
    }
    int is_gameplay = 0;
    if (g_p_mem_base && *g_p_mem_base && rh_mem_committed(0x008654a4u, 4)) {
        is_gameplay = (MR32(0x008654a4u) == 6u);
    }
    static void(*s_gpu_gov_tick)(int) = NULL;
    static int s_gpu_gov_checked = 0;
    if (!s_gpu_gov_checked) {
        s_gpu_gov_checked = 1;
        s_gpu_gov_tick = (void(*)(int))dlsym(RTLD_DEFAULT, "nfs_gpu_governor_tick");
        if (!s_gpu_gov_tick) {
            void* hp = dlopen("libperf_cores.so", RTLD_NOLOAD);
            if (!hp) hp = dlopen("libperf_cores.so", RTLD_NOW | RTLD_GLOBAL);
            if (hp) s_gpu_gov_tick = (void(*)(int))dlsym(hp, "nfs_gpu_governor_tick");
        }
    }
    if (s_gpu_gov_tick) s_gpu_gov_tick(is_gameplay);
    roam_game_frame(NULL);
    atomic_store(&s_tick_in_progress, 0);
}

/* Gamepad Right Stick Quick Chat Input & Camera Look */
void roam_stick_input(float x, float y) {
    g_stick_x = x;
    g_stick_y = y;
    if (!g_hooks_installed) {
        roam_android_init_symbols();
        roam_android_install_hooks();
    }
    static void (*s_cam_stick)(float, float) = NULL;
    static void (*s_garage_stick)(float, float) = NULL;
    if (!s_cam_stick || !s_garage_stick) {
        void *h = dlopen("libgame_core.so", RTLD_NOLOAD);
        if (!h) h = dlopen("libgame_core.so", RTLD_NOW | RTLD_GLOBAL);
        if (h) {
            if (!s_cam_stick) s_cam_stick = (void (*)(float, float))dlsym(h, "nfs_camera_stick_input");
            if (!s_garage_stick) s_garage_stick = (void (*)(float, float))dlsym(h, "nfs_garage_stick_input");
        }
    }
    if (!s_cam_stick) s_cam_stick = (void (*)(float, float))dlsym(RTLD_DEFAULT, "nfs_camera_stick_input");
    if (!s_garage_stick) s_garage_stick = (void (*)(float, float))dlsym(RTLD_DEFAULT, "nfs_garage_stick_input");
    if (s_cam_stick) s_cam_stick(x, y);
    if (s_garage_stick) s_garage_stick(x, y);

    // Only process quickchat phrase selection if quickchat menu is ALREADY open.
    // Moving the right stick while driving MUST rotate camera, NOT open quick chat!
    if (g_qc_open < 0) return;

    float r = sqrtf(x * x + y * y);
    if (r < 0.60f) return;

    int dir = (fabsf(x) > fabsf(y)) ? (x > 0.f ? 1 : 3) : (y < 0.f ? 0 : 2);
    int phrase = g_qc_open * 4 + dir;
    g_qc_open = -1;
    g_qc_open_until = 0;
    atomic_store(&g_say_pick, phrase);
}

/* JNI Interface */
#ifdef __cplusplus
extern "C" {
#endif

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_jvm = vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        jclass clz = (*env)->FindClass(env, "io/mininxd/nfsu2/net/RoamNativeBridge");
        if (!clz) {
            (*env)->ExceptionClear(env);
            clz = (*env)->FindClass(env, "io/mininxd/nfsu2/AdhocRoamClient");
        }
        if (clz) {
            g_client_class = (jclass)(*env)->NewGlobalRef(env, clz);
            g_mid_on_said = (*env)->GetStaticMethodID(env, g_client_class, "onQuickChatReceived", "(Ljava/lang/String;)V");
            g_mid_on_send = (*env)->GetStaticMethodID(env, g_client_class, "onNativeSend", "(II[B)V");
        }
    }
    roam_android_patch_carpool();
    roam_android_install_hooks();
    return JNI_VERSION_1_6;
}

static void roam_do_configure(JNIEnv *env, jstring jRoom, jstring jName, jstring jPeers, jboolean jGhostMode, jint jHubIp) {
    g_ghost_mode = (jGhostMode == JNI_TRUE) ? 1 : 0;
    g_hub_ip = (uint32_t)jHubIp;
    if (g_s && g_hub_ip) {
        roam_plat_lock();
        g_s->conf.hub_ip = g_hub_ip;
        g_s->conf.hub_port = 47210;
        g_s->conf.snap_states = 1;
        g_s->hub_hello_tx = INT64_MIN / 4;
        roam_plat_unlock();
    }
    if (!env) return;
    if (jRoom) {
        const char *r = (*(JNIEnv **)env != NULL) ? (*env)->GetStringUTFChars(env, jRoom, NULL) : NULL;
        if (r) { strncpy(g_room_name, r, sizeof g_room_name - 1); (*env)->ReleaseStringUTFChars(env, jRoom, r); }
    }
    if (jName) {
        const char *n = (*(JNIEnv **)env != NULL) ? (*env)->GetStringUTFChars(env, jName, NULL) : NULL;
        if (n) { strncpy(g_player_name, n, sizeof g_player_name - 1); (*env)->ReleaseStringUTFChars(env, jName, n); }
    }
    if (jPeers) {
        const char *p = (*(JNIEnv **)env != NULL) ? (*env)->GetStringUTFChars(env, jPeers, NULL) : NULL;
        if (p) { strncpy(g_peers_str, p, sizeof g_peers_str - 1); (*env)->ReleaseStringUTFChars(env, jPeers, p); }
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "Configured Native Roam: Room=%s, Name=%s, Peers=%s, GhostMode=%d, Hub=%08x",
                        g_room_name, g_player_name, g_peers_str, g_ghost_mode, g_hub_ip);
}

static void roam_do_receive(JNIEnv *env, jint srcIp, jint srcPort, jbyteArray jData) {
    if (!g_s || !jData) return;
    jsize len = (*env)->GetArrayLength(env, jData);
    if (len <= 0 || len > 1500) return;

    uint8_t buf[1500];
    (*env)->GetByteArrayRegion(env, jData, 0, len, (jbyte *)buf);

    int64_t now = roam_plat_now_us();
    roam_plat_lock();
    g_st.recv++;
    g_st.recv_bytes += (uint64_t)len;
    rc_receive(g_s, (uint32_t)srcIp, (uint16_t)srcPort, buf, (int)len, now);
    roam_plat_unlock();
}

static void roam_do_tick(void) {
    // Only tick the network protocol state here.
    // Game frame operations (camera, roam_game_frame, hidden races, pacing, guest calls)
    // MUST only run synchronously on the main game/render thread via Present in guest_runtime.cpp.
    // Calling game frame logic from an asynchronous Java worker thread corrupts the guest stack and allocator.
    roam_plat_lock();
    if (g_s) rc_tick(g_s, roam_plat_now_us());
    roam_plat_unlock();
}

static void roam_do_say(jint phrase) {
    if (phrase >= 0 && phrase < 16) {
        atomic_store(&g_say_pick, (int)phrase);
    }
}

static void roam_do_stick(jfloat x, jfloat y) {
    roam_stick_input(x, y);
}

static void roam_do_horn(jboolean held) {
    atomic_store(&g_horn_held, (held == JNI_TRUE) ? 1 : 0);
}

/* JNI Bindings for io.mininxd.nfsu2.AdhocRoamClient */
JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamConfigure(
        JNIEnv *env, jclass clazz, jstring jRoom, jstring jName, jstring jPeers, jboolean jGhostMode, jint jHubIp) {
    (void)clazz;
    roam_do_configure(env, jRoom, jName, jPeers, jGhostMode, jHubIp);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamReceive(
        JNIEnv *env, jclass clazz, jint srcIp, jint srcPort, jbyteArray jData) {
    (void)clazz;
    roam_do_receive(env, srcIp, srcPort, jData);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamTick(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    roam_do_tick();
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamSay(
        JNIEnv *env, jclass clazz, jint phrase) {
    (void)env; (void)clazz;
    roam_do_say(phrase);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamStick(
        JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
    (void)env; (void)clazz;
    roam_do_stick(x, y);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamHorn(
        JNIEnv *env, jclass clazz, jboolean held) {
    (void)env; (void)clazz;
    roam_do_horn(held);
}

/* JNI Bindings for io.mininxd.nfsu2.net.RoamNativeBridge */
JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeRoamConfigure(
        JNIEnv *env, jclass clazz, jstring jRoom, jstring jName, jstring jPeers, jboolean jGhostMode, jint jHubIp) {
    (void)clazz;
    roam_do_configure(env, jRoom, jName, jPeers, jGhostMode, jHubIp);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeRoamReceive(
        JNIEnv *env, jclass clazz, jint srcIp, jint srcPort, jbyteArray jData) {
    (void)clazz;
    roam_do_receive(env, srcIp, srcPort, jData);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeRoamTick(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    roam_do_tick();
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeRoamSay(
        JNIEnv *env, jclass clazz, jint phrase) {
    (void)env; (void)clazz;
    roam_do_say(phrase);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeRoamStick(
        JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
    (void)env; (void)clazz;
    roam_do_stick(x, y);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeRoamHorn(
        JNIEnv *env, jclass clazz, jboolean held) {
    (void)env; (void)clazz;
    roam_do_horn(held);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeSetFrameLimit(
        JNIEnv *env, jclass clazz, jint fps) {
    (void)env; (void)clazz;
    nfs_roam_set_frame_limit((uint32_t)fps);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeSetFrameLimit(
        JNIEnv *env, jclass clazz, jint fps) {
    (void)env; (void)clazz;
    nfs_roam_set_frame_limit((uint32_t)fps);
}

JNIEXPORT jlong JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeGetFrameCount(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    return (jlong)g_frame_count;
}

/* JNI Bindings for io.mininxd.nfsu2.net.RoamNativeBridge / GameActivity key mapping query */
static const jint DEFAULT_ACTION_KEYS[11] = {
    0xc8, /* 0: Gas (Action 1) */
    0xd0, /* 1: Brake (Action 2) */
    0xcb, /* 2: Steer Left (Action 3) */
    0xcd, /* 3: Steer Right (Action 4) */
    0x39, /* 4: Handbrake (Action 5) */
    0x38, /* 5: Nitro (Action 6) */
    0x2a, /* 6: Shift Up (Action 7) */
    0x1d, /* 7: Shift Down (Action 8) */
    0x30, /* 8: Look Back (Action 9) */
    0x2e, /* 9: Camera (Action 10) */
    0x13  /* 10: Reset Car (Action 14) */
};

static jintArray get_action_keys_array(JNIEnv *env) {
    jint keys[11];
    memcpy(keys, DEFAULT_ACTION_KEYS, sizeof(keys));

    if (roam_android_init_symbols() && g_p_mem_base && *g_p_mem_base && g_p_esp && *g_p_esp) {
        if (rh_mem_committed(0x86e89cu, 4) && rh_mem_committed(0x86f354u, 4)) {
            uint32_t table_va = MR32(0x86e89cu);
            uint32_t count = MR32(0x86f354u);
            if (table_va != 0 && count > 0 && count < 256 && rh_mem_committed(table_va, count * 40u)) {
                uint8_t filled[11] = {0};
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t entry_va = table_va + i * 40u;
                    uint32_t action = MR32(entry_va);
                    uint32_t raw_key = MR32(entry_va + 0x20u);
                    uint32_t scancode = ((raw_key >> 8) & 0xffu) ? ((raw_key >> 8) & 0xffu) : (raw_key & 0xffu);
                    if (scancode == 0) continue;

                    int idx = -1;
                    switch (action) {
                        case 1:  idx = 0; break; /* Gas */
                        case 2:  idx = 1; break; /* Brake */
                        case 3:  idx = 2; break; /* Steer Left */
                        case 4:  idx = 3; break; /* Steer Right */
                        case 5:  idx = 4; break; /* Handbrake */
                        case 6:  idx = 5; break; /* Nitro */
                        case 7:  idx = 6; break; /* Shift Up */
                        case 8:  idx = 7; break; /* Shift Down */
                        case 9:  idx = 8; break; /* Look Back */
                        case 10: idx = 9; break; /* Camera */
                        case 14: idx = 10; break; /* Reset Car */
                        default: break;
                    }

                    if (idx >= 0 && !filled[idx]) {
                        keys[idx] = (jint)scancode;
                        filled[idx] = 1;
                    }
                }
            }
        }
    }

    jintArray result = (*env)->NewIntArray(env, 11);
    if (result) {
        (*env)->SetIntArrayRegion(env, result, 0, 11, keys);
    }
    return result;
}

JNIEXPORT jintArray JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeGetActionKeys(
        JNIEnv *env, jclass clazz) {
    (void)clazz;
    return get_action_keys_array(env);
}

JNIEXPORT jintArray JNICALL Java_io_mininxd_nfsu2_GameActivity_nativeGetActionKeys(
        JNIEnv *env, jclass clazz) {
    (void)clazz;
    return get_action_keys_array(env);
}

/* JNI Bindings for io.mininxd.nfsu2.PerformanceCores */
JNIEXPORT jint JNICALL Java_io_mininxd_nfsu2_PerformanceCores_nativeBindToPerformanceCores(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    return nfs_bind_to_perf_cores();
}

JNIEXPORT jlong JNICALL Java_io_mininxd_nfsu2_PerformanceCores_nativeGetPerformanceCoresMask(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    long highest_freq = 0;
    long freqs[64] = {0};
    int total_cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (total_cpus <= 0 || total_cpus > 64) total_cpus = 8;
    for (int i = 0; i < total_cpus; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *f = fopen(path, "r");
        if (f) {
            long freq = 0;
            if (fscanf(f, "%ld", &freq) == 1 && freq > 0) {
                freqs[i] = freq;
                if (freq > highest_freq) highest_freq = freq;
            }
            fclose(f);
        }
    }
    jlong mask = 0;
    long threshold = (long)(highest_freq * 0.80);
    if (highest_freq > 0) {
        for (int i = 0; i < total_cpus; i++) {
            if (freqs[i] >= threshold) mask |= (1ULL << i);
        }
    }
    if (mask == 0) {
        int start = total_cpus > 1 ? total_cpus / 2 : 0;
        for (int i = start; i < total_cpus; i++) mask |= (1ULL << i);
    }
    return mask;
}

JNIEXPORT jint JNICALL Java_io_mininxd_nfsu2_PerformanceCores_nativeGetCoreCount(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    int c = sysconf(_SC_NPROCESSORS_CONF);
    return c > 0 ? c : 8;
}

JNIEXPORT jstring JNICALL Java_io_mininxd_nfsu2_PerformanceCores_nativeGetSummary(
        JNIEnv *env, jclass clazz) {
    (void)clazz;
    char buf[128];
    snprintf(buf, sizeof(buf), "Cores: %ld", sysconf(_SC_NPROCESSORS_CONF));
    return (*env)->NewStringUTF(env, buf);
}



/* ========================================================================= */
/* Gamepad Layout & LS/RS Controller Input Fix                               */
/* ========================================================================= */
struct RoamPadObject {
    uint32_t guidData1;
    uint32_t offset;
    uint32_t type;
    uint32_t flags;
    const char *name;
};

static const char *s_ps2_pad_names[] = {
    "Left Stick X", "Left Stick Y", "Trigger/Gas",
    "Right Stick X", "Right Stick Y", "Right Stick Z",
    "D-Pad",
    "Cross", "Circle", "Square", "Triangle",
    "L1", "R1",
    "Select", "Start",
    "L3", "R3"
};

static const char *s_xbox_pad_names[] = {
    "X Axis", "Y Axis", "Z-Axis",
    "X Rotation", "Y Rotation", "Z Rotation",
    "D-pad",
    "A", "B", "X", "Y",
    "LB", "RB",
    "Back", "Start",
    "LS", "RS"
};

__attribute__((visibility("default")))
int _ZN12GamepadInput9buttonForEi(int code) {
    switch (code) {
        case 96: case 188: case 23: return 0;   /* A, Button 1, Dpad Center -> A */
        case 97: case 189: return 1;           /* B, Button 2 -> B */
        case 99: case 190: return 2;           /* X, Button 3 -> X */
        case 100: case 191: return 3;          /* Y, Button 4 -> Y */
        case 102: case 192: return 4;          /* L1, Button 5 -> L1 */
        case 103: case 193: return 5;          /* R1, Button 6 -> R1 */
        case 104: case 194: return 6;          /* L2, Button 7 -> L2 */
        case 105: case 195: return 7;          /* R2, Button 8 -> R2 */
        case 108: case 197: case 82: return 8; /* Start, Button 10, Menu -> Start */
        case 109: case 196: case 4: return 9;  /* Select, Button 9, Back -> Select */
        case 106: case 198: case 200: case 202: return 10; /* ThumbL (106), Button 11 (198), Button 13 (200), Button 15 (202) -> ThumbL (LS / L3) */
        case 107: case 199: case 201: case 203: return 11; /* ThumbR (107), Button 12 (199), Button 14 (201), Button 16 (203) -> ThumbR (RS / R3) */
        case 19: return 12;                    /* Dpad Up */
        case 20: return 13;                    /* Dpad Down */
        case 21: return 14;                    /* Dpad Left */
        case 22: return 15;                    /* Dpad Right */
        default: return -1;
    }
}

static void roam_apply_gamepad_layout(int is_ps2) {
    void *h = dlopen("libnfsu2_android.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!h) h = dlopen("libnfsu2_android.so", RTLD_LAZY | RTLD_GLOBAL);
    void *sym = dlsym(h ? h : RTLD_DEFAULT, "recomp_lookup");
    if (!sym) sym = dlsym(h ? h : RTLD_DEFAULT, "g_mem_base");
    if (!sym) sym = dlsym(RTLD_DEFAULT, "_ZN12GamepadInput9buttonForEi");
    Dl_info dlinfo;
    if (sym && dladdr(sym, &dlinfo) && dlinfo.dli_fbase) {
        uintptr_t base = (uintptr_t)dlinfo.dli_fbase;
        uintptr_t page_size = sysconf(_SC_PAGESIZE);
        if (page_size == 0) page_size = 4096;

        /* 1. Hook GOT entry for _ZN12GamepadInput9buttonForEi (resolves LS and RS button issues) */
        void **got_entry = (void**)(base + 0x97dd968);
        uintptr_t got_page = ((uintptr_t)got_entry) & ~(page_size - 1);
        mprotect((void*)got_page, page_size, PROT_READ | PROT_WRITE);
        *got_entry = (void*)_ZN12GamepadInput9buttonForEi;

        /* 2. Patch padObjects DirectInput names table (Xbox vs PS2 names) */
        uint32_t *p = (uint32_t*)(base + 0x975d880);
        if (p[0] == 0xa36d02e0 && p[1] == 0 && p[2] == 0x2) {
            uintptr_t pad_page = (base + 0x975d880) & ~(page_size - 1);
            mprotect((void*)pad_page, page_size, PROT_READ | PROT_WRITE);
            struct RoamPadObject *objs = (struct RoamPadObject*)(base + 0x975d880);
            const char **names = is_ps2 ? s_ps2_pad_names : s_xbox_pad_names;
            for (int i = 0; i < 17; i++) {
                objs[i].name = names[i];
            }
        }

        /* 3. Patch device name ("PlayStation 2" vs "Xbox Controller") */
        char *dev_name = (char*)(base + 0x18511e);
        uintptr_t dev_name_page = ((uintptr_t)dev_name) & ~(page_size - 1);
        mprotect((void*)dev_name_page, page_size, PROT_READ | PROT_WRITE);
        if (is_ps2) {
            memcpy(dev_name, "PlayStation 2\0", 14);
        } else {
            memcpy(dev_name, "Xbox Controller", 16);
        }

        __android_log_print(ANDROID_LOG_INFO, "NFSU2",
            "Applied %s gamepad layout (DirectInput padObjects updated, LS/RS buttons bound to _ZN12GamepadInput9buttonForEi)",
            is_ps2 ? "PS2 Style" : "Xbox Default");
    }
}

JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeGamepadLayout(
        JNIEnv *env, jclass clazz, jboolean console) {
    (void)env; (void)clazz;
    roam_apply_gamepad_layout(console == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_GameActivity_nativeGamepadLayout(
        JNIEnv *env, jclass clazz, jboolean console) {
    Java_com_nfsu2_androidevolved_GameActivity_nativeGamepadLayout(env, clazz, console);
}

__attribute__((constructor)) static void powervr_guard_ctor(void) {
    install_powervr_fault_guard();
    install_guest_null_fault_guard();
    roam_apply_gamepad_layout(0);

    pthread_t th;
    if (pthread_create(&th, NULL, guest_null_page_guard_worker, NULL) == 0) {
        pthread_detach(th);
    }
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_GameActivity_nativeGpuDriver(
        JNIEnv *env, jclass clazz, jstring hooks, jstring temp, jstring directory, jstring library) {
    typedef void (*fn_gpu_driver)(JNIEnv*, jclass, jstring, jstring, jstring, jstring);
    static fn_gpu_driver s_gpu_driver = NULL;
    static int s_resolved = 0;
    if (!s_resolved) {
        s_resolved = 1;
        s_gpu_driver = (fn_gpu_driver)dlsym(RTLD_DEFAULT, "Java_com_nfsu2_androidevolved_GameActivity_nativeGpuDriver");
    }
    if (s_gpu_driver) {
        s_gpu_driver(env, clazz, hooks, temp, directory, library);
    }
    install_powervr_fault_guard();
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_GameActivity_nativeKeyNav(
        JNIEnv *env, jclass clazz, jint scan, jboolean down) {
    (void)env; (void)clazz;
    if (down && (scan == 0xcb || scan == 0xcd)) {
        roam_resolution_nav(scan == 0xcd ? 1 : -1);
    }
}

JNIEXPORT jfloat JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeGetCarSpeed(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    uint32_t car = get_driving_car();
    if (!car) return 0.0f;
    return get_car_speed_mps(car) * 3.6f;
}

JNIEXPORT jfloat JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeGetCarSpeedKmH(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    uint32_t car = get_driving_car();
    if (!car) return 0.0f;
    return get_car_speed_mps(car) * 3.6f;
}

JNIEXPORT jint JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeGetCarNos(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    uint32_t car = get_driving_car();
    if (!car || !g_p_mem_base || !*g_p_mem_base) return 0;
    uint32_t n = car + 0x40u;
    if (!rh_mem_committed(n + 0x414u, 16)) return 0;
    uint32_t full = MR32(n + 0x414u), left = MR32(n + 0x41cu);
    if (full == 0) return 0;
    return (jint)((left * 100u) / full);
}

JNIEXPORT jboolean JNICALL Java_io_mininxd_nfsu2_net_RoamNativeBridge_nativeIsNosActive(
        JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    uint32_t car = get_driving_car();
    if (!car || !g_p_mem_base || !*g_p_mem_base) return JNI_FALSE;
    uint32_t n = car + 0x40u;
    if (!rh_mem_committed(n + 0x420u, 4)) return JNI_FALSE;
    return (MR32(n + 0x420u) != 0) ? JNI_TRUE : JNI_FALSE;
}

#ifdef __cplusplus
}
#endif
