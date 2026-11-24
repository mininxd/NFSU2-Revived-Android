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
static int g_hooks_installed = 0;
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
        if (!s_lib_base && g_p_recomp_lookup) {
            Dl_info dlinfo;
            if (dladdr((void *)g_p_recomp_lookup, &dlinfo) && dlinfo.dli_fbase) {
                s_lib_base = (uintptr_t)dlinfo.dli_fbase;
            }
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
static recomp_void_func_t s_orig_664a1f = NULL;
static recomp_hook_func_t s_orig_recomp_hook = NULL;

static recomp_void_func_t arm64_inline_hook(void *target_func, void *hook_func) {
    if (!target_func || !hook_func) return NULL;
    uint32_t *target = (uint32_t *)target_func;

    static uint8_t *s_tramp_page = NULL;
    static size_t s_tramp_offset = 0;
    if (!s_tramp_page) {
        s_tramp_page = (uint8_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (s_tramp_page == MAP_FAILED) {
            s_tramp_page = NULL;
            __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Hook", "mmap trampoline page failed");
            return NULL;
        }
    }

    uint8_t *tramp = s_tramp_page + s_tramp_offset;
    s_tramp_offset += 64;

    // 1. Copy first 4 instructions (16 bytes) to trampoline
    memcpy(tramp, target, 16);

    // 2. Append branch to target + 16 (4 instructions)
    uint32_t *t_code = (uint32_t *)(tramp + 16);
    t_code[0] = 0x58000050; // ldr x16, [pc, #8]
    t_code[1] = 0xd61f0200; // br x16
    *(uint64_t *)(tramp + 24) = (uint64_t)((uintptr_t)target + 16);

    __builtin___clear_cache((char *)tramp, (char *)tramp + 32);

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
}

static void hook_sub_005caad0(void) {
    void *h_gc = dlopen("libgraphics_cache.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!h_gc) h_gc = dlopen("libgraphics_cache.so", RTLD_LAZY | RTLD_GLOBAL);
    void (*fn_bypass)(int) = h_gc ? (void (*)(int))dlsym(h_gc, "nfs_camera_cull_set_bypass") : NULL;
    if (fn_bypass) fn_bypass(1);

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

    static void (*s_set_contrast_active)(uint32_t) = NULL;
    if (!s_set_contrast_active) {
        if (h_gc) s_set_contrast_active = (void (*)(uint32_t))dlsym(h_gc, "nfs_contrast_set_active");
        if (!s_set_contrast_active) {
            void *h_core = dlopen("libgame_core.so", RTLD_NOLOAD | RTLD_GLOBAL);
            if (!h_core) h_core = dlopen("libgame_core.so", RTLD_LAZY | RTLD_GLOBAL);
            if (h_core) s_set_contrast_active = (void (*)(uint32_t))dlsym(h_core, "nfs_contrast_set_active");
        }
    }

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

    if (fn_bypass) fn_bypass(0);
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

/* =========================================================================
 * D3DX9 Preshader & State Evaluation Memoization (HLE Parity with nfsu2-web)
 * ========================================================================= */
#define D3DX_MEMO_BUF 256u
typedef struct {
    uint32_t th;
    uint32_t ab;
    uint32_t eax;
    uint32_t n;
    uint32_t dst;
    uint32_t dval;
    uint32_t vval;
    uint32_t len;
    uint32_t valid;
    uint8_t  buf[D3DX_MEMO_BUF];
} D3dxMemoSlot;

#define D3DX_MEMO_CAP 4096u
static D3dxMemoSlot *g_d3dx_memo = NULL;
static uint64_t g_memo_calls = 0;
static uint64_t g_memo_hits = 0;
static uint64_t g_memo_skips = 0;

typedef struct { uint8_t *d; uint32_t len, cap; int eq; } D3dxSnapW;

static inline int d3dx_sw_put(D3dxSnapW *w, const void *p, uint32_t n) {
    if (w->len + n > w->cap) return 0;
    uint8_t *q = w->d + w->len;
    const uint8_t *s = (const uint8_t *)p;
    if (w->eq) {
        uint64_t x = 0;
        uint32_t i = 0;
        for (; i + 8u <= n; i += 8u) {
            uint64_t a, b;
            memcpy(&a, q + i, 8);
            memcpy(&b, s + i, 8);
            x |= a ^ b;
        }
        for (; i + 4u <= n; i += 4u) {
            uint32_t a, b;
            memcpy(&a, q + i, 4);
            memcpy(&b, s + i, 4);
            x |= a ^ b;
        }
        for (; i < n; i++) x |= (uint64_t)(q[i] ^ s[i]);
        if (x) {
            w->eq = 0;
            memcpy(q, s, n);
        }
    } else {
        memcpy(q, s, n);
    }
    w->len += n;
    return 1;
}

static inline int d3dx_state_input_snap(D3dxSnapW *w, uint32_t th, uint32_t P, uint32_t R) {
    if (!rh_mem_committed(R, 0x50u) || !rh_mem_committed(P, 0x10u)) return 0;
    uint32_t fix[6] = { th, R, MR32(R + 0x38u), MR32(P), MR32(R + 0x48u), MR32(R + 8u) };
    if (!d3dx_sw_put(w, fix, sizeof(fix))) return 0;
    if (MR32(R + 0x3cu)) {
        unsigned n = MR16(R + 0x4eu);
        uint32_t base = MR32(R + 0x34u);
        if (!base || !rh_mem_committed(base, n * 32u)) return 0;
        for (unsigned i = 0; i < n; i++) {
            uint32_t rec = base + i * 32u;
            if (!MR32(rec)) continue;
            uint32_t pp = MR32(rec + 0xcu);
            if (!pp || !rh_mem_committed(pp, 8)) continue;
            uint32_t dp = MR32(pp), ds = MR32(pp + 4u);
            if (!ds || !rh_mem_committed(ds, 0x18u)) continue;
            uint32_t el = MR32(ds + 0x10u); if (!el) el = 1;
            uint32_t rows = MR32(ds + 0x14u); if (!rows) rows = 1;
            uint32_t bytes = dp ? el * rows * 16u : 0u;
            uint32_t hdr[2] = { MR32(rec), dp };
            if (bytes > w->cap || !d3dx_sw_put(w, hdr, sizeof(hdr))) return 0;
            if (bytes && (!rh_mem_committed(dp, bytes) || !d3dx_sw_put(w, GPTR(dp), bytes))) return 0;
        }
    }
    return 1;
}

static inline uint32_t d3dx_rd32s(uint32_t a) {
    return (a && rh_mem_committed(a, 4)) ? MR32(a) : 0u;
}

static void hook_sub_00664a1f(void) {
    if (!g_p_esp || !*g_p_esp || !g_p_ecx || !g_p_mem_base || !*g_p_mem_base) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }
    uint32_t esp = *g_p_esp;
    if (!rh_mem_committed(esp, 12)) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }

    if (!g_d3dx_memo) {
        g_d3dx_memo = (D3dxMemoSlot *)calloc(D3DX_MEMO_CAP, sizeof(D3dxMemoSlot));
        if (!g_d3dx_memo) {
            if (s_orig_664a1f) s_orig_664a1f();
            return;
        }
    }

    uint32_t th = *g_p_ecx;
    if (!th || !rh_mem_committed(th, 0x30u)) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }

    uint32_t a = MR32(esp + 4u);
    uint32_t b = MR32(esp + 8u);

    uint32_t p_table = MR32(th + 0x2cu);
    if (!p_table || !rh_mem_committed(p_table + a * 0x64u, 8u)) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }
    uint32_t P = a * 0x64u + p_table;

    uint32_t r_table = MR32(P + 4u);
    if (!r_table || !rh_mem_committed(r_table + b * 0x64u, 0x50u)) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }
    uint32_t R = b * 0x64u + r_table;

    g_memo_calls++;
    if ((MR8(R + 0x30u) & 4) || !MR32(R + 0x38u)) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }

    uint32_t ab = (a << 16) | (b & 0xffffu);
    uint32_t hk = (th * 0x9E3779B1u) ^ (ab * 0x85EBCA77u);
    D3dxMemoSlot *s = NULL;
    for (uint32_t j = 0; j < 64u; j++) {
        D3dxMemoSlot *t = &g_d3dx_memo[(hk + j) & (D3DX_MEMO_CAP - 1)];
        if (!t->n || (t->th == th && t->ab == ab)) { s = t; break; }
    }
    if (!s) {
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }

    int mine = s->n && s->th == th && s->ab == ab && s->valid;
    D3dxSnapW w = { s->buf, 0, D3DX_MEMO_BUF, mine };
    if (!d3dx_state_input_snap(&w, th, P, R)) {
        s->valid = 0; s->th = th; s->ab = ab; s->n++;
        if (s_orig_664a1f) s_orig_664a1f();
        return;
    }

    uint32_t dst = MR32(P) + MR32(R + 0x48u);
    uint32_t vb = MR32(R + 8u);
    int same = mine && w.eq && w.len == s->len;
    int hit = same && s->dst == dst && d3dx_rd32s(dst) == s->dval && d3dx_rd32s(vb) == s->vval;

    if (hit) {
        g_memo_hits++;
        g_memo_skips++;
        if (g_p_eax) *g_p_eax = s->eax;
        *g_p_esp = esp + 12u; // ret 8: pop return address (4) + 2 parameters (8)
        return;
    }

    s->th = th; s->ab = ab; s->valid = 0; s->n++;
    if (s_orig_664a1f) {
        s_orig_664a1f();
    }
    s->len = w.len;
    s->eax = g_p_eax ? *g_p_eax : 0;
    s->valid = 1;
    s->dst = dst;
    s->dval = d3dx_rd32s(dst);
    s->vval = d3dx_rd32s(vb);
}

static void roam_android_install_hooks(void) {
    if (g_hooks_installed || !g_p_recomp_lookup) return;

    Dl_info dlinfo;
    if (!dladdr((void *)g_p_recomp_lookup, &dlinfo) || !dlinfo.dli_fbase) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU2-Hook", "Failed to resolve lib base via dladdr");
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

    // 6. sub_00664a1f (D3DX9 preshader state value memo: eliminates ~48% CPU thread overhead)
    void *target_66 = g_p_recomp_lookup ? (void *)g_p_recomp_lookup(0x00664a1fu) : NULL;
    if (target_66) {
        s_orig_664a1f = (recomp_void_func_t)arm64_inline_hook(target_66, (void *)hook_sub_00664a1f);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Hook", "Hooked sub_00664a1f (D3DX9 state memo) at %p (orig: %p)", target_66, s_orig_664a1f);
    }

    g_hooks_installed = 1;
}

static const uint32_t kAllResolutionPercents[] = {
    25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 125, 150, 200
};
#define NUM_ALL_RESOLUTION_PERCENTS (sizeof(kAllResolutionPercents) / sizeof(kAllResolutionPercents[0]))

static int s_res_window_start = -1;
static uint32_t s_target_pct = 75;

static void update_resolution_slots_in_memory(void) {
    if (!g_p_mem_base || !*g_p_mem_base) return;
    if (!rh_mem_committed(0x800538u, 24) || !rh_mem_committed(0x800550u, 24) || !rh_mem_committed(0x86e880u, 24)) return;

    const uint32_t label_vas[6] = {0x78fa1cu, 0x78fa14u, 0x78fa08u, 0x78f9fcu, 0x78f9f0u, 0x78f9e4u};
    const uint32_t label_caps[6] = {8, 8, 12, 12, 12, 12};

    if (s_res_window_start < 0) s_res_window_start = 0;
    if (s_res_window_start > (int)NUM_ALL_RESOLUTION_PERCENTS - 6) s_res_window_start = (int)NUM_ALL_RESOLUTION_PERCENTS - 6;

    for (int i = 0; i < 6; i++) {
        uint32_t pct = kAllResolutionPercents[s_res_window_start + i];
        uint32_t w = s_display_w > 0 ? ((s_display_w * pct / 100u) & ~1u) : 1280;
        uint32_t h = s_display_h > 0 ? ((s_display_h * pct / 100u) & ~1u) : 720;
        if (w < 320) w = 320;
        if (h < 240) h = 240;

        MR32(0x800538u + i * 4) = w;
        MR32(0x800550u + i * 4) = h;
        MR32(0x86e880u + i * 4) = 1;

        if (rh_mem_committed(label_vas[i], label_caps[i])) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u%%", pct);
            memset(GPTR(label_vas[i]), 0, label_caps[i]);
            strncpy((char *)GPTR(label_vas[i]), buf, label_caps[i] - 1);
        }
    }
}

void roam_resolution_nav(int dir) {
    if (!g_p_mem_base || !*g_p_mem_base || !rh_mem_committed(0x870d1cu, 4)) return;
    // Only navigate resolution in menus (MR32(SPLIT_PLAYERS_INDEX) == 0 means not in race/world)
    if (rh_mem_committed(SPLIT_PLAYERS_INDEX, 4) && MR32(SPLIT_PLAYERS_INDEX) != 0) return;

    uint32_t cur = MR32(0x870d1cu);

    if (dir > 0 && cur >= 5 && s_res_window_start < (int)NUM_ALL_RESOLUTION_PERCENTS - 6) {
        s_res_window_start++;
        update_resolution_slots_in_memory();
        MR32(0x870d1cu) = 4;
        if (rh_mem_committed(0x8709d1u, 1)) MR8(0x8709d1u) = 1;
    } else if (dir < 0 && cur == 0 && s_res_window_start > 0) {
        s_res_window_start--;
        update_resolution_slots_in_memory();
        MR32(0x870d1cu) = 1;
        if (rh_mem_committed(0x8709d1u, 1)) MR8(0x8709d1u) = 1;
    }
}

static void resolution_frame_tick(void) {
    if (!g_p_mem_base || !*g_p_mem_base || !rh_mem_committed(0x86e880u, 24)) return;

    // Ensure all 6 slots are always marked as available (fixes Issue #41)
    for (int i = 0; i < 6; i++) {
        MR32(0x86e880u + i * 4) = 1;
    }

    static int s_res_initialized = 0;
    if (!s_res_initialized) {
        s_res_initialized = 1;
        const char *w_str = getenv("NFSU2_DISPLAY_WIDTH");
        const char *h_str = getenv("NFSU2_DISPLAY_HEIGHT");
        const char *p_str = getenv("NFSU2_RES_PERCENT");

        if (w_str) s_display_w = (uint32_t)atoi(w_str);
        if (h_str) s_display_h = (uint32_t)atoi(h_str);
        if (p_str) s_target_pct = (uint32_t)atoi(p_str);
        if (s_target_pct == 0) s_target_pct = 75;

        int best_idx = 0;
        int best_diff = 999999;
        for (size_t i = 0; i < NUM_ALL_RESOLUTION_PERCENTS; i++) {
            int diff = abs((int)kAllResolutionPercents[i] - (int)s_target_pct);
            if (diff < best_diff) {
                best_diff = diff;
                best_idx = (int)i;
            }
        }

        s_res_window_start = best_idx - 2;
        if (s_res_window_start < 0) s_res_window_start = 0;
        if (s_res_window_start > (int)NUM_ALL_RESOLUTION_PERCENTS - 6) s_res_window_start = (int)NUM_ALL_RESOLUTION_PERCENTS - 6;

        update_resolution_slots_in_memory();

        int active_slot = best_idx - s_res_window_start;
        if (active_slot < 0) active_slot = 0;
        if (active_slot > 5) active_slot = 5;

        if (rh_mem_committed(0x870d1cu, 4)) {
            MR32(0x870d1cu) = (uint32_t)active_slot;
        }
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
