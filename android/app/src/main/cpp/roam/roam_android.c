#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "roam_host_android.h"
#include "roam_core.h"
#include "roam_plat.h"
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
recomp_func_t (*g_p_recomp_lookup)(uint32_t va) = NULL;
int (*g_p_guest_is_readable)(uint32_t va, uint32_t bytes) = NULL;

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
#endif

#define G_CARPOOL_KB     0x007fa9d4u
#define CARPOOL_KB       48000
#define G_RIDE_SLOT_CAP  0x0061c744u

static int g_carpool_patched = 0;

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
        g_p_guest_is_readable = (int (*)(uint32_t, uint32_t))dlsym(h, "guest_is_readable");
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
        if (!rh_mem_committed(0x008709a0u, 4)) return;
        uint32_t pl = MR32(0x008709a0u);
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

/* Hidden Races Mod: unhides 33 career races & raises stage caps to 8 */
#define G_RACES         0x008383e0u   /* races, RACE_SZ each, [G_NRACES] of them */
#define G_NRACES        0x008383e4u
#define G_STAGES        0x0083841cu   /* stages, STAGE_SZ each, [G_NSTAGES] of them */
#define G_NSTAGES       0x00838418u
#define RACE_SZ         0x88u
#define STAGE_SZ        0x50u
#define R_HIDDEN        0x7fu         /* race: 1 hidden */
#define S_CAPS          0x30u         /* stage: 5 caps, a signed byte each */
#define CAP             8

static int g_hidden_on = -1;
static uint32_t g_hidden_done = 0;

static void hiddenraces_frame_tick(void) {
    if (__builtin_expect(g_hidden_done != 0, 1)) return;
    if (g_hidden_on < 0) {
        const char *e = getenv("NFSU2_SHOW_HIDDEN_RACES");
        g_hidden_on = e && *e && *e != '0' && strcasecmp(e, "off") && strcasecmp(e, "no") && strcasecmp(e, "false");
    }
    if (!g_hidden_on) return;
    if (!rh_mem_committed(G_RACES, 4) || !rh_mem_committed(G_STAGES, 4)) return;
    uint32_t races = MR32(G_RACES), nr = MR32(G_NRACES), stages = MR32(G_STAGES), ns = MR32(G_NSTAGES);
    if (!races || races == g_hidden_done || nr > 1024u || ns > 16u || !rh_mem_committed(races, nr * RACE_SZ) || !rh_mem_committed(stages, ns * STAGE_SZ))
        return;
    int n = 0, caps = 0;
    for (uint32_t i = 0; i < nr; i++) {
        if (MR8(races + i * RACE_SZ + R_HIDDEN)) {
            MR8(races + i * RACE_SZ + R_HIDDEN) = 0;
            n++;
        }
    }
    for (uint32_t s = 0; s < ns; s++) {
        for (uint32_t k = 0; k < 5u; k++) {
            uint32_t a = stages + s * STAGE_SZ + S_CAPS + k;
            int8_t v = (int8_t)MR8(a);
            if (v >= 0 && v < CAP) {
                MR8(a) = CAP;
                caps++;
            }
        }
    }
    g_hidden_done = races;
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "[career] show hidden races: %d of %u races out of hiding, %d caps raised to %d",
                        n, nr, caps, CAP);
}

/* Extra Options (ExOpts Team) Memory Patches & Dumb AI (AutoPilot) Hack */
static void extraoptions_init_patches(void) {
    if (!rh_mem_committed(0x00400000u, 0x1000)) return;

    // Optical drive / Disc 2 bypass
    if (rh_mem_committed(0x005C0D3Fu, 1)) MR8(0x005C0D3Fu) = 0x78;
    if (rh_mem_committed(0x005C0D54u, 1)) MR8(0x005C0D54u) = 0x00;

    // Ground Shadow Alignment Fix (0.015f instead of 0.04f to sit flush with road)
    if (rh_mem_committed(0x007A66B4u, 4)) MRF32(0x007A66B4u) = 0.015f;

    // Lap Controllers (0 to 127 laps)
    if (rh_mem_committed(0x004B3D82u, 30)) {
        MR8(0x004B3D82u) = 0;   // min lap decrement
        MR8(0x004B3D9Bu) = 0;   // min lap increment
        MR8(0x004B3D86u) = 127; // max lap decrement
        MR8(0x004B3D97u) = 127; // max lap increment
    }
    if (rh_mem_committed(0x004B407Fu, 1)) MR8(0x004B407Fu) = 5; // KO laps

    // LAN Laps (0 to 127)
    if (rh_mem_committed(0x004B3E7Au, 1)) MR8(0x004B3E7Au) = 0;
    if (rh_mem_committed(0x004B3EE4u, 1)) MR8(0x004B3EE4u) = 0;
    if (rh_mem_committed(0x004B3E9Eu, 1)) MR8(0x004B3E9Eu) = 0;
    if (rh_mem_committed(0x004B3F30u, 1)) MR8(0x004B3F30u) = 127;
    if (rh_mem_committed(0x004B3F41u, 1)) MR8(0x004B3F41u) = 127;

    // Opponent Controllers (0 to 5 opponents)
    if (rh_mem_committed(0x004B382Bu, 70)) {
        MR32(0x004B382Bu) = 0x90909090u;
        MR16(0x004B382Fu) = 0x9090u;
        MR8(0x004B3834u) = 5; // Max CPU
        MR8(0x004B3863u) = 0; // Min CPU
        MR32(0x004B3867u) = 0x90909090u;
    }
    if (rh_mem_committed(0x004B4075u, 1)) MR8(0x004B4075u) = 5; // KO CPU

    // LAN Players (up to 6)
    if (rh_mem_committed(0x004B31AAu, 4)) {
        MR8(0x004B31AAu) = 0xB0;
        MR8(0x004B31ABu) = 6;
        MR16(0x004B31ACu) = 0x9090u;
    }

    // Unfreeze KO (ShowMoreRaceOptions)
    if (rh_mem_committed(0x004B4068u, 1)) MR8(0x004B4068u) = 0xEB;
    if (rh_mem_committed(0x004B3E14u, 1)) MR8(0x004B3E14u) = 0xEB;
    if (rh_mem_committed(0x004B3903u, 1)) MR8(0x004B3903u) = 0xEB;

    // Regional Cars (Peugeot 106 & Opel Corsa unlocked on NTSC)
    if (rh_mem_committed(0x007F7C48u, 16)) {
        MR32(0x007F7C48u) = 0xFFFFFFFFu; // CORSA
        MR32(0x007F7C4Cu) = 0xFFFFFFFFu; // PEUGOT106
        MR32(0x007F7C50u) = 0xFFFFFFFFu; // CIVIC
        MR32(0x007F7C54u) = 0xFFFFFFFFu; // RSX
    }
    if (rh_mem_committed(0x004FEB9Du, 1)) MR8(0x004FEB9Du) = 0x1C;
    if (rh_mem_committed(0x007F6DBEu, 2)) {
        MR8(0x007F6DBEu) = 0x07;
        MR8(0x007F6DBFu) = 0x06;
    }

    // Carbon-style 1-Lap race progress ("COMPLETED %X")
    if (rh_mem_committed(0x004AA27Fu, 8)) {
        MR8(0x004AA27Fu) = 0xBC;
        MR8(0x004AA283u) = 0x3C;
        MR8(0x004AA284u) = 0x01;
        MR8(0x004AA286u) = 0x85;
    }

    // Menu: Free Run track select, Outrun in Quick Race, Outrun track select
    if (rh_mem_committed(0x004B2AE3u, 2)) MR16(0x004B2AE3u) = 0x9090u;
    if (rh_mem_committed(0x004B2F3Eu, 2)) MR16(0x004B2F3Eu) = 0x9090u;
    if (rh_mem_committed(0x004B2AA0u, 2)) MR16(0x004B2AA0u) = 0x9090u;
    if (rh_mem_committed(0x005268F8u, 2)) MR16(0x005268F8u) = 0x9090u;

    // Subtitles
    if (rh_mem_committed(0x004A8BA0u, 2)) MR16(0x004A8BA0u) = 0x9090u;
    if (rh_mem_committed(0x00553A65u, 2)) MR16(0x00553A65u) = 0x9090u;
    if (rh_mem_committed(0x0055B061u, 2)) MR16(0x0055B061u) = 0x9090u;

    // 3D Fixes: damaged cracked glass, disappearing wheels, megalow parts
    if (rh_mem_committed(0x0059FC13u, 1)) MR8(0x0059FC13u) = 0xEB;
    if (rh_mem_committed(0x0049B61Eu, 1)) MR8(0x0049B61Eu) = 0xEB;
    if (rh_mem_committed(0x0054FBD7u, 1)) MR8(0x0054FBD7u) = 0xEB;

    // Drift: show drift as AxB, plus sign, max 9x multiplier
    if (rh_mem_committed(0x004AB792u, 3)) {
        MR8(0x004AB792u) = 0xB0;
        MR8(0x004AB793u) = 0x01;
        MR8(0x004AB794u) = 0x90;
    }
    if (rh_mem_committed(0x004AB7DBu, 1)) MR8(0x004AB7DBu) = 0x74;
    if (rh_mem_committed(0x0056CC98u, 10)) {
        MR8(0x0056CC98u) = 0xCF;
        MR8(0x0056CC9Cu) = 0x00;
        MR8(0x0056CC9Du) = 0x50;
        MR8(0x0056CC9Eu) = 0x92;
        MR8(0x0056CC58u) = 9;
        MR8(0x0056CC90u) = 9;
    }
    if (rh_mem_committed(0x00925004u, 32 * 4)) {
        static const int thresholds[32] = {
            350, 1400, 4200, 11200, 22400, 38080, 57120, 85680,
            300, 1200, 3600, 9600, 19200, 32640, 48960, 73440,
            250, 1000, 3000, 8000, 16000, 27200, 40800, 61200,
            200, 800, 2400, 6400, 12800, 21760, 32640, 48960
        };
        for (int i = 0; i < 32; i++) {
            MR32(0x00925004u + (uint32_t)(i * 4)) = (uint32_t)thresholds[i];
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "NFSU2-ExtraOptions",
                        "Extra Options memory patches applied: regional cars, laps (0-127), opponents (0-5), 3D fixes, drift 9x, outrun");
}

static void extraoptions_frame_tick(void) {
    static int s_init_done = 0;
    if (!s_init_done) {
        if (rh_mem_committed(0x00400000u, 0x1000)) {
            s_init_done = 1;
            extraoptions_init_patches();
        }
    }

    if (!rh_mem_committed(0x008654A4u, 4) || !rh_mem_committed(0x008900ACu, 4)) return;

    // Dumb AI (AutoDrive) State Machine
    int dumb_ai = 0;
    const char *e = getenv("NFSU2_DUMB_AI");
    if (e && *e && *e != '0' && strcasecmp(e, "off") && strcasecmp(e, "no") && strcasecmp(e, "false")) {
        dumb_ai = 1;
    }

    uint32_t flow = MR32(0x008654A4u); // TheGameFlowManager (6 = Gameplay)
    static uint32_t s_applied_player = 0;
    static int s_applied_state = -1;

    if (flow != 6) {
        s_applied_player = 0;
        s_applied_state = -1;
        return;
    }

    uint32_t player = MR32(0x008900ACu); // _PlayersByNumber[0]
    if (!player) {
        // Player not ready yet in this race frame; reset state so we apply immediately when player appears
        s_applied_player = 0;
        s_applied_state = -1;
        return;
    }

    if (s_applied_player != player || s_applied_state != dumb_ai) {
        if (dumb_ai) {
            // Player_AutoPilotOn (0x005FAE20) with ecx = player (__thiscall)
            roam_android_call(0x005FAE20u, player, 0, 0, NULL);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-ExtraOptions",
                                "Dumb AI (AutoPilot) ENABLED on player car 0x%08x", player);
        } else {
            // Player_AutoPilotOff (0x005FAF10) with ecx = player
            roam_android_call(0x005FAF10u, player, 0, 0, NULL);
            // Player_SetInputMode (0x00605C70) with ecx = player, mode = 2
            uint32_t arg = 2;
            roam_android_call(0x00605C70u, player, 0, 1, &arg);
            __android_log_print(ANDROID_LOG_INFO, "NFSU2-ExtraOptions",
                                "Dumb AI (AutoPilot) DISABLED on player car 0x%08x", player);
        }
        s_applied_player = player;
        s_applied_state = dumb_ai;
    }
}

/* In-game menu button scaling (Help bar and arrows up to 200%) */
#define O_NEXT    0x04u
#define O_TYPE    0x18u
#define O_FLAGS   0x1cu
#define O_DATA    0x2cu
#define O_HASH    0x10u
#define P_OBJS    0x4cu
#define D_POS     0x1cu
#define D_SIZE    0x38u
#define T_IMAGE   1u
#define T_STRING  2u
#define F_DIRTY   0x400000u
#define FRAME_W   100.f
#define MAX_SCALE 2.5f
#define MAX_OBJS  64
#define KEPT_MAX  4096

typedef struct { uint32_t data; long seen; float own[4], put[4]; } Kept;
static Kept g_kept[KEPT_MAX];
static int  g_nkept = 0;
static float g_last_menu_scale = 1.0f;

static inline uint32_t ms_field(int j) { return (j < 2 ? D_POS : D_SIZE) + 4u * (uint32_t)(j & 1); }

static Kept *ms_kept(uint32_t d) {
    Kept *k = NULL;
    for (int i = 0; i < g_nkept; i++) {
        if (g_kept[i].data == d) { k = &g_kept[i]; break; }
    }
    if (!k) {
        if (g_nkept < KEPT_MAX) k = &g_kept[g_nkept++];
        else {
            k = &g_kept[0];
            for (int i = 1; i < KEPT_MAX; i++) if (g_kept[i].seen < k->seen) k = &g_kept[i];
        }
        k->data = d;
        for (int j = 0; j < 4; j++) k->own[j] = k->put[j] = MRF32(d + ms_field(j));
    }
    for (int j = 0; j < 4; j++) {
        float cur = MRF32(d + ms_field(j));
        if (cur != k->put[j]) k->own[j] = cur;
    }
    k->seen = g_frame_count;
    return k;
}

static void ms_put(uint32_t o, Kept *k, const float want[4]) {
    int changed = 0;
    for (int j = 0; j < 4; j++) {
        if (MRF32(k->data + ms_field(j)) != want[j]) {
            MRF32(k->data + ms_field(j)) = want[j];
            changed = 1;
        }
        k->put[j] = want[j];
    }
    if (changed) MR32(o + O_FLAGS) |= F_DIRTY;
}

static void ms_apply_bar(uint32_t pkg, float s) {
    if (s <= 1.001f && g_last_menu_scale <= 1.001f) return;
    uint32_t obj[MAX_OBJS];
    Kept *k[MAX_OBJS];
    uint32_t otype[MAX_OBJS];
    int n = 0;

    float min_x = 999999.0f, max_x = -999999.0f;
    float min_y = 999999.0f, max_y = -999999.0f;
    int have_bounds = 0;

    for (uint32_t o = MR32(pkg + P_OBJS); o && n < MAX_OBJS && rh_mem_committed(o, 0x30u); o = MR32(o + O_NEXT)) {
        uint32_t t = MR32(o + O_TYPE), d = MR32(o + O_DATA);
        if ((t != T_IMAGE && t != T_STRING) || !rh_mem_committed(d, 0x44u)) continue;
        Kept *kp = ms_kept(d);
        if (!kp) continue;
        obj[n] = o;
        k[n] = kp;
        otype[n] = t;

        float cx = kp->own[0] + (t == T_IMAGE ? kp->own[2] * 0.5f : 0.0f);
        float cy = kp->own[1] + (t == T_IMAGE ? kp->own[3] * 0.5f : 0.0f);
        if (cx < min_x) min_x = cx;
        if (cx > max_x) max_x = cx;
        if (cy < min_y) min_y = cy;
        if (cy > max_y) max_y = cy;
        have_bounds = 1;
        n++;
    }

    if (!have_bounds || n == 0) return;

    float group_cx = (min_x + max_x) * 0.5f;
    float group_cy = (min_y + max_y) * 0.5f;

    for (int i = 0; i < n; i++) {
        const float *own = k[i]->own;
        uint32_t t = otype[i];
        float want[4];

        if (t == T_IMAGE) {
            float obj_cx = own[0] + own[2] * 0.5f;
            float obj_cy = own[1] + own[3] * 0.5f;
            float new_cx = group_cx + (obj_cx - group_cx) * s;
            float new_cy = group_cy + (obj_cy - group_cy) * s;
            float new_w = own[2] * s;
            float new_h = own[3] * s;
            want[0] = new_cx - new_w * 0.5f;
            want[1] = new_cy - new_h * 0.5f;
            want[2] = new_w;
            want[3] = new_h;
            /* Clamp to screen margins so buttons never clip beyond screen bounds */
            if (want[0] + want[2] > 316.0f) want[0] = 316.0f - want[2];
            if (want[0] < -316.0f) want[0] = -316.0f;
            if (want[1] + want[3] > 234.0f) want[1] = 234.0f - want[3];
            if (want[1] < -234.0f) want[1] = -234.0f;
        } else {
            // T_STRING: position is text origin; size is font scale
            float obj_cx = own[0];
            float obj_cy = own[1];
            float new_cx = group_cx + (obj_cx - group_cx) * s;
            float new_cy = group_cy + (obj_cy - group_cy) * s;
            want[0] = new_cx;
            want[1] = new_cy;
            want[2] = own[2] * s;
            want[3] = own[3] * s;
            if (want[0] > 310.0f) want[0] = 310.0f;
            if (want[0] < -310.0f) want[0] = -310.0f;
            if (want[1] > 230.0f) want[1] = 230.0f;
            if (want[1] < -230.0f) want[1] = -230.0f;
        }

        ms_put(obj[i], k[i], want);
    }
}

static const uint32_t ARROWS[] = { 0x444969fdu, 0x444969feu };
static void ms_apply_arrows(uint32_t pkg, float s) {
    if (s <= 1.001f && g_last_menu_scale <= 1.001f) return;
    int n = 0;
    for (uint32_t o = MR32(pkg + P_OBJS); o && n < MAX_OBJS && rh_mem_committed(o, 0x30u); o = MR32(o + O_NEXT), n++) {
        uint32_t h = MR32(o + O_HASH), d = MR32(o + O_DATA);
        if (MR32(o + O_TYPE) != T_IMAGE || (h != ARROWS[0] && h != ARROWS[1]) || !rh_mem_committed(d, 0x44u)) continue;
        Kept *k = ms_kept(d);
        if (!k) continue;
        float cx = k->own[0] + k->own[2] * 0.5f;
        float cy = k->own[1] + k->own[3] * 0.5f;
        float nw = k->own[2] * s;
        float nh = k->own[3] * s;
        float want[4] = { cx - nw * 0.5f, cy - nh * 0.5f, nw, nh };
        ms_put(o, k, want);
    }
}

#ifdef ROAM_CORE_RUST
extern float nfs_pacing_filter_dt(float real_elapsed, float current_wdt);
#endif

static float ms_scale_now(void) {
    static float s_cached = -1.0f;
    if (__builtin_expect(s_cached >= 1.0f, 1)) return s_cached;
    const char *e = getenv("NFSU2_MENU_SCALE");
    if (!e || !*e) { s_cached = 1.5f; return 1.5f; }
    float s = (float)atof(e);
    if (s > 10.0f) s /= 100.0f;
    if (s < 1.0f) s = 1.0f;
    if (s > MAX_SCALE) s = MAX_SCALE;
    s_cached = s;
    return s;
}

static void menusize_frame_tick(void) {
    /* 200% button sizing and per-frame UI mutation disabled to prevent
     * flickering and EventHandlerSlotPool exhaustion. */
    return;
}

static void framerate_pacing_fix(void) {
#ifdef ROAM_CORE_RUST
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
        return;
    }
#else
    if (!rh_mem_committed(0x007875bcu, 4) || !rh_mem_committed(0x00865558u, 4)) return;
    /* 0x007875BC is the game's simulation time step base (60 Hz).
     * Must be locked strictly to 1/60s so internal clocks (A_WTF, particles)
     * maintain full real speed regardless of 30/60/uncapped render rates. */
    MRF32(0x007875bcu) = 0.016666668f;

    /* Disarm internal busy-spin framerate limiters */
    MRF32(0x00865558u) = 0.0f;
    if (rh_mem_committed(0x007fb710u, 4)) MRF32(0x007fb710u) = 0.0f;

    /* Force NTSC 60 Hz mode (0x7fb700 = 1). Prevents PAL 50 Hz lock (20 ms clamp). */
    if (rh_mem_committed(0x007fb700u, 4)) MR32(0x007fb700u) = 1;

    /* Expand maximum allowable frame delta multiplier (0x7fb714).
     * Default 3.0f clamps frames to 3*16.67ms = 50 ms (20 FPS), causing slow motion
     * whenever render rate drops to 30 FPS or lower. 6.0f allows up to 100 ms (10 FPS)
     * so 30 FPS or frame drops maintain real-time speed. */
    if (rh_mem_committed(0x007fb714u, 4)) MRF32(0x007fb714u) = 6.0f;

    /* High refresh rate patch at 0x0057A084:
     * Change 'jne 0x57a08b' (0x75) to 'jmp 0x57a08b' (0xEB).
     * Prevents spurious 'mov eax, 1' when frame time is under 1/60s (90/120/uncapped FPS). */
    static int s_high_fps_patched = 0;
    if (!s_high_fps_patched && rh_mem_committed(0x0057a084u, 1)) {
        if (MR8(0x0057a084u) == 0x75) {
            MR8(0x0057a084u) = 0xEB;
        }
        s_high_fps_patched = 1;
    }

    /* Clamp g_CarHeadlightEnable (0x00870cc4) to 1 (Player only), preventing NPC cars from casting projector headlights */
    if (rh_mem_committed(0x00870cc4u, 4) && MR32(0x00870cc4u) > 1) {
        MR32(0x00870cc4u) = 1;
    }

    /* Trackside Crowd Suppression (0x00870cd0 = g_CrowdEnable):
     * Suppress 3D spectator models and alpha-tested fence crowds around stadium corners
     * to prevent vertex transform and alpha test fillrate drops on mobile GPUs. */
    static int s_crowd_env_checked = 0;
    if (!s_crowd_env_checked) {
        s_crowd_env_checked = 1;
        const char *env_crowd = getenv("NFSU2_CROWD");
        int allow_crowd = (env_crowd && (strcmp(env_crowd, "1") == 0 || strcasecmp(env_crowd, "on") == 0));
        if (!allow_crowd) {
            MR32(0x00870cd0u) = 0;
        }
    } else if (rh_mem_committed(0x00870cd0u, 4)) {
        const char *env_crowd = getenv("NFSU2_CROWD");
        int allow_crowd = (env_crowd && (strcmp(env_crowd, "1") == 0 || strcasecmp(env_crowd, "on") == 0));
        if (!allow_crowd) {
            MR32(0x00870cd0u) = 0;
        }
    }

    /* Prevent slow motion during racing: track real wall-clock elapsed time. */
    static int64_t s_last_tick_us = 0;
    int64_t now_us = roam_plat_now_us();
    if (s_last_tick_us > 0) {
        float real_elapsed = (float)(now_us - s_last_tick_us) / 1000000.0f;
        if (real_elapsed > 0.010f && real_elapsed < 0.200f) {
            if (rh_mem_committed(0x00865154u, 4) && rh_mem_committed(0x00865198u, 4)) {
                float current_wdt = MRF32(0x00865154u);
                float current_realdt = MRF32(0x00865198u);
                float effective_dt = (current_wdt > 0.0f) ? current_wdt : current_realdt;
                if (effective_dt > 0.0f && effective_dt < real_elapsed * 0.90f) {
                    MRF32(0x00865154u) = real_elapsed;
                    MRF32(0x00865198u) = real_elapsed;
                    if (rh_mem_committed(0x00865140u, 4)) {
                        MR32(0x00865140u) = (uint32_t)(real_elapsed * 1800.0f);
                    }
                }
            }
        }
    }
    s_last_tick_us = now_us;
#endif
}

static volatile uint32_t s_target_fps = 0;

__attribute__((visibility("default"))) void nfs_roam_set_frame_limit(uint32_t fps) {
    s_target_fps = (fps >= 30 && fps <= 240) ? fps : 0;
}

static void pace_frame(void) {
    /* Pacing is handled cooperatively by host FramePacer in guest_runtime via yieldGuestMachine.
     * Avoid blocking nanosleep here which double-waits and starves guest background threads. */
}

static float g_look_yaw = 0.f;
static float g_stick_x = 0.f;
static float g_stick_y = 0.f;
static uint32_t g_turned_cam = 0;
static uint8_t g_keep_cam[0x70];
static uint8_t g_made_cam[0x70];
static int g_cam_turned = 0;

static void look_turn_xy(float *p, int pt, float ox, float oy, float yaw) {
    float c = cosf(yaw), s = sinf(yaw);
    float px = pt ? ox : 0.f, py = pt ? oy : 0.f;
    float x = p[0] - px, y = p[1] - py;
    p[0] = px + x * c + y * s;
    p[1] = py - x * s + y * c;
}

static void look_turn_matrix(uint32_t cam, float ox, float oy, float yaw) {
    float c = cosf(yaw), s = -sinf(yaw);
    float *M = (float *)GPTR(cam);
    float tx = ox - (ox * c + oy * s), ty = oy - (-ox * s + oy * c);
    float r0[4], r1[4];
    for (int k = 0; k < 4; k++) {
        r0[k] = c * M[k] - s * M[4 + k];
        r1[k] = s * M[k] + c * M[4 + k];
    }
    for (int k = 0; k < 4; k++) {
        M[12 + k] += tx * M[k] + ty * M[4 + k];
        M[k] = r0[k]; M[4 + k] = r1[k];
    }
}

static void look_camera_frame_tick(void) {
    if (!g_p_esp || !*g_p_esp || !g_p_mem_base || !*g_p_mem_base) return;
    if (g_cam_turned && g_turned_cam && rh_mem_committed(g_turned_cam, sizeof(g_made_cam))) {
        if (!memcmp(GPTR(g_turned_cam), g_made_cam, sizeof(g_made_cam))) {
            memcpy(GPTR(g_turned_cam), g_keep_cam, sizeof(g_keep_cam));
        }
        g_cam_turned = 0;
    }

    float r = hypotf(g_stick_x, g_stick_y);
    float target_yaw = 0.f;
    if (r > 0.25f && g_qc_open < 0) {
        float k = fminf(1.f, (r - 0.25f) / 0.60f);
        target_yaw = atan2f(g_stick_x, -g_stick_y) * k;
    }
    g_look_yaw += (target_yaw - g_look_yaw) * 0.25f;
    if (fabsf(g_look_yaw) < 0.001f && fabsf(target_yaw) < 0.001f) g_look_yaw = 0.f;

    if (fabsf(g_look_yaw) > 0.005f && g_p_mem_base && *g_p_mem_base) {
        if (!rh_mem_committed(0x008709a0u, 4)) return;
        uint32_t pl = MR32(0x008709a0u); // G_PLAYER1
        if (pl && rh_mem_committed(pl + 0x238u, 4) && rh_mem_committed(pl + 4u, 4)) {
            uint32_t mover = MR32(pl + 0x238u);
            uint32_t cam = (mover && rh_mem_committed(mover + 0x40u, 4)) ? MR32(mover + 0x40u) : 0;
            uint32_t car = MR32(pl + 4u);
            if (cam && car && rh_mem_committed(cam, sizeof(g_keep_cam)) && rh_mem_committed(car + 0x60u, 16)) {
                float *car_pos = (float *)GPTR(car + 0x60u);
                float ox = car_pos[0], oy = car_pos[1];
                memcpy(g_keep_cam, GPTR(cam), sizeof(g_keep_cam));
                look_turn_matrix(cam, ox, oy, g_look_yaw);
                look_turn_xy((float *)GPTR(cam + 0x40u), 1, ox, oy, g_look_yaw);
                look_turn_xy((float *)GPTR(cam + 0x50u), 0, ox, oy, g_look_yaw);
                look_turn_xy((float *)GPTR(cam + 0x60u), 1, ox, oy, g_look_yaw);
                memcpy(g_made_cam, GPTR(cam), sizeof(g_made_cam));
                g_turned_cam = cam;
                g_cam_turned = 1;
            }
        }
    }
}

static const uint32_t kAllResolutionPercents[] = {
    25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 125, 150, 200
};
#define NUM_ALL_RESOLUTION_PERCENTS (sizeof(kAllResolutionPercents) / sizeof(kAllResolutionPercents[0]))

static int s_res_window_start = -1;
static uint32_t s_display_w = 0, s_display_h = 0;
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
    // Only navigate resolution in menus (MR32(0x008709a0u) == 0 means not in race/world)
    if (rh_mem_committed(0x008709a0u, 4) && MR32(0x008709a0u) != 0) return;

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
    if (atomic_exchange(&s_tick_in_progress, 1) != 0) return;
    pace_frame();
    static int s_perf_bound = 0;
    if (!s_perf_bound || (g_frame_count % 300) == 0) {
        s_perf_bound = 1;
        nfs_bind_to_perf_cores();
    }
    g_frame_count++;
    hiddenraces_frame_tick();
    extraoptions_frame_tick();
    lobby_frame_tick();
    framerate_pacing_fix();
    resolution_frame_tick();
    look_camera_frame_tick();
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
    float r = sqrtf(x * x + y * y);
    if (r < 0.35f) {
        g_qc_armed = 1;
        return;
    }
    if (r < 0.8f || !g_qc_armed) return;
    g_qc_armed = 0;

    int dir = (fabsf(x) > fabsf(y)) ? (x > 0.f ? 1 : 3) : (y < 0.f ? 0 : 2);
    int64_t now = roam_plat_now_us();

    if (g_qc_open < 0 || now > g_qc_open_until) {
        g_qc_open = dir;
        g_qc_open_until = now + 4000000; // 4 seconds open
        char b[64];
        snprintf(b, sizeof b, "{\"group\":%d}", dir);
        roam_plat_said(b);
    } else {
        int phrase = g_qc_open * 4 + dir;
        g_qc_open = -1;
        g_qc_open_until = 0;
        atomic_store(&g_say_pick, phrase);
    }
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

/* Performance cores CPU affinity binding */
int nfs_bind_to_perf_cores(void) {
    long highest_freq = 0;
    long freqs[64] = {0};
    int total_cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (total_cpus <= 0 || total_cpus > 64) total_cpus = 8;

    for (int i = 0; i < total_cpus; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *f = fopen(path, "r");
        if (!f) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
            f = fopen(path, "r");
        }
        if (!f) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", i);
            f = fopen(path, "r");
            if (f) {
                long cap = 0;
                if (fscanf(f, "%ld", &cap) == 1 && cap > 0) {
                    freqs[i] = cap * 1000;
                    if (freqs[i] > highest_freq) highest_freq = freqs[i];
                }
                fclose(f);
                continue;
            }
        }
        if (f) {
            long freq = 0;
            if (fscanf(f, "%ld", &freq) == 1 && freq > 0) {
                freqs[i] = freq;
                if (freq > highest_freq) highest_freq = freq;
            }
            fclose(f);
        }
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int count = 0;
    long threshold = (long)(highest_freq * 0.80);

    if (highest_freq > 0) {
        for (int i = 0; i < total_cpus; i++) {
            if (freqs[i] >= threshold) {
                CPU_SET(i, &cpuset);
                count++;
            }
        }
    }

    if (count == 0) {
        int start = total_cpus > 1 ? total_cpus / 2 : 0;
        for (int i = start; i < total_cpus; i++) {
            CPU_SET(i, &cpuset);
            count++;
        }
    }

    sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    // Apply performance core affinity to all worker threads of the process
    DIR *d = opendir("/proc/self/task");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            int tid = atoi(de->d_name);
            if (tid > 0) {
                sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
            }
        }
        closedir(d);
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-PerfCores", "Bound process and threads to %d performance cores (ret=%d)", count, ret);
    return ret;
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

JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeDrawDistance(
        JNIEnv *env, jclass clazz, jfloat distance) {
    (void)env; (void)clazz;
    typedef void (*fn_set_dd)(float);
    static fn_set_dd s_set_dd = NULL;
    static int s_resolved = 0;
    if (!s_resolved) {
        s_resolved = 1;
        s_set_dd = (fn_set_dd)dlsym(RTLD_DEFAULT, "nfs_widescreen_set_draw_distance");
    }
    if (s_set_dd) {
        s_set_dd((float)distance);
    }
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_GameActivity_nativeDrawDistance(
        JNIEnv *env, jclass clazz, jfloat distance) {
    Java_com_nfsu2_androidevolved_GameActivity_nativeDrawDistance(env, clazz, distance);
}

/* ========================================================================= */
/* PowerVR / IMG BXM Vulkan Pipeline Creation Fault Guard                    */
/* ========================================================================= */
typedef void* VkInstance_t;
typedef void* VkDevice_t;
typedef void* VkPhysicalDevice_t;
typedef uint64_t VkPipelineCache_t;
typedef uint64_t VkPipeline_t;
typedef void (*PFN_vkVoidFunction_t)(void);
typedef PFN_vkVoidFunction_t (*PFN_vkGetInstanceProcAddr_t)(VkInstance_t instance, const char* pName);
typedef PFN_vkVoidFunction_t (*PFN_vkGetDeviceProcAddr_t)(VkDevice_t device, const char* pName);
typedef int32_t VkResult_t;

typedef struct {
    uint32_t robustBufferAccess;
    uint32_t fullDrawIndexUint32;
    uint32_t imageCubeArray;
    uint32_t independentBlend;
    uint32_t geometryShader;
    uint32_t tessellationShader;
    uint32_t sampleRateShading;
    uint32_t dualSrcBlend;
    uint32_t logicOp;
    uint32_t multiDrawIndirect;
    uint32_t drawIndirectFirstInstance;
    uint32_t depthClamp;
    uint32_t depthBiasClamp;
    uint32_t fillModeNonSolid;
    uint32_t depthBounds;
    uint32_t wideLines;
    uint32_t largePoints;
    uint32_t alphaToOne;
    uint32_t multiViewport;
    uint32_t samplerAnisotropy;
    uint32_t textureCompressionETC2;
    uint32_t textureCompressionASTC_LDR;
    uint32_t textureCompressionBC;
    uint32_t occlusionQueryPrecise;
    uint32_t pipelineStatisticsQuery;
    uint32_t vertexPipelineStoresAndAtomics;
    uint32_t fragmentStoresAndAtomics;
    uint32_t shaderTessellationAndGeometryPointSize;
    uint32_t shaderImageGatherExtended;
    uint32_t shaderStorageImageExtendedFormats;
    uint32_t shaderStorageImageMultisample;
    uint32_t shaderStorageImageReadWithoutFormat;
    uint32_t shaderStorageImageWriteWithoutFormat;
    uint32_t shaderUniformBufferArrayDynamicIndexing;
    uint32_t shaderSampledImageArrayDynamicIndexing;
    uint32_t shaderStorageBufferArrayDynamicIndexing;
    uint32_t shaderStorageImageArrayDynamicIndexing;
    uint32_t shaderClipDistance;
    uint32_t shaderCullDistance;
    uint32_t shaderFloat64;
    uint32_t shaderInt64;
    uint32_t shaderInt16;
    uint32_t shaderResourceResidency;
    uint32_t shaderResourceMinLod;
    uint32_t sparseBinding;
    uint32_t sparseResidencyBuffer;
    uint32_t sparseResidencyImage2D;
    uint32_t sparseResidencyImage3D;
    uint32_t sparseResidency2Samples;
    uint32_t sparseResidency4Samples;
    uint32_t sparseResidency8Samples;
    uint32_t sparseResidency16Samples;
    uint32_t sparseResidencyAliased;
    uint32_t variableMultisampleRate;
    uint32_t inheritedQueries;
} VkPhysicalDeviceFeatures_compat_t;

typedef struct {
    uint32_t sType;
    const void* pNext;
    uint32_t flags;
    uint32_t queueCreateInfoCount;
    const void* pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
    const VkPhysicalDeviceFeatures_compat_t* pEnabledFeatures;
} VkDeviceCreateInfo_compat_t;

typedef VkResult_t (*PFN_vkCreateDevice_t)(
    VkPhysicalDevice_t physicalDevice,
    const VkDeviceCreateInfo_compat_t* pCreateInfo,
    const void* pAllocator,
    VkDevice_t* pDevice);

typedef void (*PFN_vkGetPhysicalDeviceFeatures_t)(
    VkPhysicalDevice_t physicalDevice,
    VkPhysicalDeviceFeatures_compat_t* pFeatures);

typedef VkResult_t (*PFN_vkCreateGraphicsPipelines_t)(
    VkDevice_t device,
    VkPipelineCache_t pipelineCache,
    uint32_t createInfoCount,
    const void* pCreateInfos,
    const void* pAllocator,
    VkPipeline_t* pPipelines);

static PFN_vkGetInstanceProcAddr_t s_real_vkGetInstanceProcAddr = NULL;
static PFN_vkGetDeviceProcAddr_t s_real_vkGetDeviceProcAddr = NULL;
static PFN_vkCreateDevice_t s_real_vkCreateDevice = NULL;
static PFN_vkGetPhysicalDeviceFeatures_t s_real_vkGetPhysicalDeviceFeatures = NULL;
static PFN_vkCreateGraphicsPipelines_t s_real_vkCreateGraphicsPipelines = NULL;

static VkResult_t guarded_vkCreateDevice(
    VkPhysicalDevice_t physicalDevice,
    const VkDeviceCreateInfo_compat_t* pCreateInfo,
    const void* pAllocator,
    VkDevice_t* pDevice) {
    if (!s_real_vkCreateDevice) return -3; /* VK_ERROR_INITIALIZATION_FAILED */
    if (!pCreateInfo) return s_real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    VkDeviceCreateInfo_compat_t safeInfo = *pCreateInfo;
    VkPhysicalDeviceFeatures_compat_t safeFeatures;

    if (pCreateInfo->pEnabledFeatures) {
        memcpy(&safeFeatures, pCreateInfo->pEnabledFeatures, sizeof(safeFeatures));
        VkPhysicalDeviceFeatures_compat_t supported;
        memset(&supported, 0, sizeof(supported));

        if (s_real_vkGetPhysicalDeviceFeatures) {
            s_real_vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
        }

        if (!supported.occlusionQueryPrecise && safeFeatures.occlusionQueryPrecise) {
            __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                "PowerVR Guard: stripped unsupported occlusionQueryPrecise from vkCreateDevice (PowerVR / IMG BXM fix)");
            safeFeatures.occlusionQueryPrecise = 0;
        }
        if (!supported.sampleRateShading && safeFeatures.sampleRateShading) {
            __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                "PowerVR Guard: stripped unsupported sampleRateShading from vkCreateDevice");
            safeFeatures.sampleRateShading = 0;
        }
        if (!supported.geometryShader && safeFeatures.geometryShader) {
            safeFeatures.geometryShader = 0;
        }
        if (!supported.multiViewport && safeFeatures.multiViewport) {
            safeFeatures.multiViewport = 0;
        }
        /* Ensure robustBufferAccess matches physical device capabilities for Adreno stability */
        if (supported.robustBufferAccess) {
            safeFeatures.robustBufferAccess = 1;
        } else {
            safeFeatures.robustBufferAccess = 0;
        }
        safeInfo.pEnabledFeatures = &safeFeatures;
    }

    typedef struct {
        uint32_t sType;
        void* pNext;
        uint32_t robustBufferAccess2;
        uint32_t robustImageAccess2;
        uint32_t nullDescriptor;
    } VkPhysicalDeviceRobustness2FeaturesEXT_compat_t;

    const void* cur_next = pCreateInfo->pNext;
    while (cur_next) {
        const uint32_t* header = (const uint32_t*)cur_next;
        if (header[0] == 1000286000 /* VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT */) {
            const VkPhysicalDeviceRobustness2FeaturesEXT_compat_t* rob2 =
                (const VkPhysicalDeviceRobustness2FeaturesEXT_compat_t*)cur_next;
            __android_log_print(ANDROID_LOG_INFO, "NFSU2",
                "Adreno/Vulkan: VK_EXT_robustness2 active in vkCreateDevice (robustBufferAccess2=%u, robustImageAccess2=%u, nullDescriptor=%u)",
                rob2->robustBufferAccess2, rob2->robustImageAccess2, rob2->nullDescriptor);
            break;
        }
        cur_next = *((const void* const*)((const char*)cur_next + 8));
    }

    return s_real_vkCreateDevice(physicalDevice, &safeInfo, pAllocator, pDevice);
}

static __thread sigjmp_buf s_powervr_pipeline_jmp_buf;
static __thread volatile sig_atomic_t s_powervr_in_pipeline_create = 0;
static struct sigaction s_old_sa_segv;
static struct sigaction s_old_sa_bus;
static pthread_mutex_t s_powervr_sig_mu = PTHREAD_MUTEX_INITIALIZER;
static int s_powervr_handler_installed = 0;

static void powervr_sig_handler(int sig, siginfo_t *info, void *ucontext) {
    if (s_powervr_in_pipeline_create) {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2",
            "PowerVR Fault Guard: trapped signal %d (fault addr: %p) during vkCreateGraphicsPipelines, recovering...",
            sig, info ? info->si_addr : NULL);
        siglongjmp(s_powervr_pipeline_jmp_buf, sig);
    }
    struct sigaction *old_sa = (sig == SIGBUS) ? &s_old_sa_bus : &s_old_sa_segv;
    if (old_sa->sa_flags & SA_SIGINFO) {
        if (old_sa->sa_sigaction) old_sa->sa_sigaction(sig, info, ucontext);
    } else if (old_sa->sa_handler && old_sa->sa_handler != SIG_DFL && old_sa->sa_handler != SIG_IGN) {
        old_sa->sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static void ensure_powervr_signal_handler(void) {
    pthread_mutex_lock(&s_powervr_sig_mu);
    if (!s_powervr_handler_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = powervr_sig_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &s_old_sa_segv);
        sigaction(SIGBUS, &sa, &s_old_sa_bus);
        s_powervr_handler_installed = 1;
    }
    pthread_mutex_unlock(&s_powervr_sig_mu);
}

static VkResult_t guarded_vkCreateGraphicsPipelines(
    VkDevice_t device,
    VkPipelineCache_t pipelineCache,
    uint32_t createInfoCount,
    const void *pCreateInfos,
    const void *pAllocator,
    VkPipeline_t *pPipelines) {

    if (!s_real_vkCreateGraphicsPipelines) {
        void *libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (libvulkan) {
            s_real_vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines_t)dlsym(libvulkan, "vkCreateGraphicsPipelines");
        }
    }
    if (!s_real_vkCreateGraphicsPipelines) {
        return -3; /* VK_ERROR_INITIALIZATION_FAILED */
    }

    ensure_powervr_signal_handler();

    s_powervr_in_pipeline_create = 1;
    int sig = sigsetjmp(s_powervr_pipeline_jmp_buf, 1);
    VkResult_t res;

    if (sig == 0) {
        res = s_real_vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "NFSU2",
            "PowerVR Fault Guard: trapped crash (sig %d). Retrying with VK_NULL_HANDLE pipeline cache...", sig);
        s_powervr_in_pipeline_create = 1;
        int sig2 = sigsetjmp(s_powervr_pipeline_jmp_buf, 1);
        if (sig2 == 0) {
            res = s_real_vkCreateGraphicsPipelines(device, 0, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "NFSU2",
                "PowerVR Fault Guard: pipeline creation aborted safely (sig %d). Returning VK_ERROR_INITIALIZATION_FAILED", sig2);
            s_powervr_in_pipeline_create = 0;
            if (pPipelines && createInfoCount > 0) {
                for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = 0;
            }
            return -3; /* VK_ERROR_INITIALIZATION_FAILED */
        }
    }
    s_powervr_in_pipeline_create = 0;
    return res;
}

static PFN_vkVoidFunction_t powervr_hook_vkGetDeviceProcAddr(VkDevice_t device, const char *pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkCreateGraphicsPipelines") == 0) {
        if (s_real_vkGetDeviceProcAddr) {
            PFN_vkCreateGraphicsPipelines_t real_fn = (PFN_vkCreateGraphicsPipelines_t)s_real_vkGetDeviceProcAddr(device, pName);
            if (real_fn) s_real_vkCreateGraphicsPipelines = real_fn;
        }
        return (PFN_vkVoidFunction_t)guarded_vkCreateGraphicsPipelines;
    }
    if (s_real_vkGetDeviceProcAddr) {
        return s_real_vkGetDeviceProcAddr(device, pName);
    }
    return NULL;
}

static PFN_vkVoidFunction_t powervr_hook_vkGetInstanceProcAddr(VkInstance_t instance, const char *pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkCreateDevice") == 0) {
        if (s_real_vkGetInstanceProcAddr) {
            PFN_vkCreateDevice_t real_cd = (PFN_vkCreateDevice_t)s_real_vkGetInstanceProcAddr(instance, pName);
            if (real_cd) s_real_vkCreateDevice = real_cd;
        }
        if (s_real_vkGetInstanceProcAddr && !s_real_vkGetPhysicalDeviceFeatures) {
            s_real_vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures_t)s_real_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures");
        }
        return (PFN_vkVoidFunction_t)guarded_vkCreateDevice;
    }
    if (strcmp(pName, "vkCreateGraphicsPipelines") == 0) {
        if (s_real_vkGetInstanceProcAddr) {
            PFN_vkCreateGraphicsPipelines_t real_fn = (PFN_vkCreateGraphicsPipelines_t)s_real_vkGetInstanceProcAddr(instance, pName);
            if (real_fn) s_real_vkCreateGraphicsPipelines = real_fn;
        }
        return (PFN_vkVoidFunction_t)guarded_vkCreateGraphicsPipelines;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        if (s_real_vkGetInstanceProcAddr) {
            PFN_vkGetDeviceProcAddr_t real_gdpa = (PFN_vkGetDeviceProcAddr_t)s_real_vkGetInstanceProcAddr(instance, pName);
            if (real_gdpa) s_real_vkGetDeviceProcAddr = real_gdpa;
        }
        return (PFN_vkVoidFunction_t)powervr_hook_vkGetDeviceProcAddr;
    }
    if (s_real_vkGetInstanceProcAddr) {
        return s_real_vkGetInstanceProcAddr(instance, pName);
    }
    return NULL;
}

static void install_powervr_fault_guard(void) {
    typedef void (*fn_set_ipa_t)(PFN_vkGetInstanceProcAddr_t);
    typedef PFN_vkGetInstanceProcAddr_t (*fn_get_ipa_t)(void);

    fn_set_ipa_t set_ipa = (fn_set_ipa_t)dlsym(RTLD_DEFAULT, "dxvkAndroidSetInstanceProcAddr");
    fn_get_ipa_t get_ipa = (fn_get_ipa_t)dlsym(RTLD_DEFAULT, "dxvkAndroidGetInstanceProcAddr");
    if (!set_ipa || !get_ipa) return;

    PFN_vkGetInstanceProcAddr_t current = get_ipa();
    if (current == powervr_hook_vkGetInstanceProcAddr) return;

    s_real_vkGetInstanceProcAddr = current;
    void *libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (libvulkan) {
        PFN_vkCreateDevice_t sys_cd = (PFN_vkCreateDevice_t)dlsym(libvulkan, "vkCreateDevice");
        if (sys_cd) s_real_vkCreateDevice = sys_cd;
        PFN_vkGetPhysicalDeviceFeatures_t sys_gpf = (PFN_vkGetPhysicalDeviceFeatures_t)dlsym(libvulkan, "vkGetPhysicalDeviceFeatures");
        if (sys_gpf) s_real_vkGetPhysicalDeviceFeatures = sys_gpf;
        PFN_vkCreateGraphicsPipelines_t sys_cgp = (PFN_vkCreateGraphicsPipelines_t)dlsym(libvulkan, "vkCreateGraphicsPipelines");
        if (sys_cgp) s_real_vkCreateGraphicsPipelines = sys_cgp;
        PFN_vkGetDeviceProcAddr_t sys_gdpa = (PFN_vkGetDeviceProcAddr_t)dlsym(libvulkan, "vkGetDeviceProcAddr");
        if (sys_gdpa) s_real_vkGetDeviceProcAddr = sys_gdpa;
    }

    set_ipa(powervr_hook_vkGetInstanceProcAddr);
    __android_log_print(ANDROID_LOG_INFO, "NFSU2", "PowerVR Fault Guard installed successfully for DXVK");
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
    roam_apply_gamepad_layout(0);
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

#ifdef __cplusplus
}
#endif
