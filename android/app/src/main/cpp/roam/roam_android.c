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
#include <jni.h>

ptrdiff_t *g_p_mem_base = NULL;
uint32_t  *g_p_esp = NULL;
uint32_t  *g_p_eax = NULL;
uint32_t  *g_p_ecx = NULL;
uint32_t  *g_p_edx = NULL;
recomp_func_t (*g_p_recomp_lookup)(uint32_t va) = NULL;

int nfs_bind_to_perf_cores(void);

#define G_CARPOOL_KB 0x007fa9d4u
#define CARPOOL_KB   48000

static int g_online_patched = 0;
static int g_carpool_patched = 0;

/* Online Mode Lobby Patch: removes '*' from "*pcnfs05.ea.com" at 0x7FBDBCu
 * so DirtySock connects via plain TCP on port 20920 instead of SSL 2.0 on 20921 */
static void roam_android_patch_online(void) {
    if (g_online_patched || !g_p_mem_base || !*g_p_mem_base) return;
    uint8_t *p = GPTR(0x7FBDBCu);
    if (memcmp(p, "*pcnfs05.ea.com", 15) == 0) {
        memcpy(p, "pcnfs05.ea.com\0", 15);
        g_online_patched = 1;
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "Patched 0x7FBDBC: removed '*' from *pcnfs05.ea.com for plain TCP lobby");
    }
}

static inline void roam_android_patch_carpool(void) {
    if (!g_carpool_patched && g_p_mem_base && *g_p_mem_base) {
        if (MR32(G_CARPOOL_KB) < CARPOOL_KB) {
            MR32(G_CARPOOL_KB) = CARPOOL_KB;
            g_carpool_patched = 1;
        }
    }
}

int roam_android_init_symbols(void) {
    if (__builtin_expect(g_p_mem_base && g_p_recomp_lookup && g_p_esp && *g_p_mem_base != 0, 1)) {
        if (!g_carpool_patched) roam_android_patch_carpool();
        if (!g_online_patched) roam_android_patch_online();
        return 1;
    }

    void *h = dlopen("libnfsu2_android.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!h) h = dlopen("libnfsu2_android.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!h) h = RTLD_DEFAULT;

    if (!g_p_mem_base) g_p_mem_base = (ptrdiff_t *)dlsym(h, "g_mem_base");
    if (!g_p_recomp_lookup) g_p_recomp_lookup = (recomp_func_t (*)(uint32_t))dlsym(h, "recomp_lookup");
    if (!g_p_esp) g_p_esp = (uint32_t *)dlsym(h, "g_esp");
    if (!g_p_eax) g_p_eax = (uint32_t *)dlsym(h, "g_eax");
    if (!g_p_ecx) g_p_ecx = (uint32_t *)dlsym(h, "g_ecx");
    if (!g_p_edx) g_p_edx = (uint32_t *)dlsym(h, "g_edx");

    if (g_p_mem_base && *g_p_mem_base != 0 && g_p_recomp_lookup && g_p_esp) {
        if (!g_carpool_patched) roam_android_patch_carpool();
        if (!g_online_patched) roam_android_patch_online();
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

static inline int rh_mem_committed(uint32_t a, uint32_t n) {
    return g_p_mem_base && *g_p_mem_base && a >= 0x00010000u && (a + n) >= 0x00010000u;
}

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
#define KEPT_MAX  512

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
    uint32_t obj[MAX_OBJS]; Kept *k[MAX_OBJS]; int n = 0;
    float px = 0.f, py = 0.f; int pivot = 0;
    for (uint32_t o = MR32(pkg + P_OBJS); o && n < MAX_OBJS && rh_mem_committed(o, 0x30u); o = MR32(o + O_NEXT)) {
        uint32_t t = MR32(o + O_TYPE), d = MR32(o + O_DATA);
        if ((t != T_IMAGE && t != T_STRING) || !rh_mem_committed(d, 0x44u) || !(k[n] = ms_kept(d))) continue;
        if (t == T_IMAGE && k[n]->own[2] > FRAME_W) {
            float r = k[n]->own[0] + k[n]->own[2] * 0.5f, b = k[n]->own[1] + k[n]->own[3] * 0.5f;
            if (!pivot || r > px) px = r;
            if (!pivot || b > py) py = b;
            pivot = 1;
        }
        obj[n++] = o;
    }
    for (int i = 0; pivot && i < n; i++) {
        const float *own = k[i]->own;
        float want[4] = { px + (own[0] - px) * s, py + (own[1] - py) * s, own[2] * s, own[3] * s };
        ms_put(obj[i], k[i], want);
    }
}

static const uint32_t ARROWS[] = { 0x444969fdu, 0x444969feu };
static void ms_apply_arrows(uint32_t pkg, float s) {
    int n = 0;
    for (uint32_t o = MR32(pkg + P_OBJS); o && n < MAX_OBJS && rh_mem_committed(o, 0x30u); o = MR32(o + O_NEXT), n++) {
        uint32_t h = MR32(o + O_HASH), d = MR32(o + O_DATA);
        if (MR32(o + O_TYPE) != T_IMAGE || (h != ARROWS[0] && h != ARROWS[1]) || !rh_mem_committed(d, 0x44u)) continue;
        Kept *k = ms_kept(d);
        float want[4] = { k->own[0], k->own[1], k->own[2] * s, k->own[3] * s };
        ms_put(o, k, want);
    }
}

static float ms_scale_now(void) {
    const char *e = getenv("NFSU2_MENU_SCALE");
    if (!e || !*e) return 1.5f;
    float s = (float)atof(e);
    if (s > 10.0f) s /= 100.0f;
    if (s < 1.0f) s = 1.0f;
    if (s > MAX_SCALE) s = MAX_SCALE;
    return s;
}

static void menusize_frame_tick(void) {
    if ((g_frame_count & 3) != 0) return;
    if (!rh_mem_committed(0x008384c4u, 4)) return;
    uint32_t fe = MR32(0x008384c4u);
    if (!fe || !rh_mem_committed(fe + 12, 4)) return;
    uint32_t mg = MR32(fe + 8u);
    if (!mg || !rh_mem_committed(mg, 0x100u)) return;

    float s = ms_scale_now();
    if (s == 1.0f && !g_nkept) return;

    if (fabsf(s - g_last_menu_scale) > 0.01f) {
        __android_log_print(ANDROID_LOG_INFO, "NFSU2-Roam", "[menusize] active menu button scale: %.0f%%", (double)(s * 100.0f));
        g_last_menu_scale = s;
    }

    for (uint32_t head = 0xe4u; head <= 0xf4u; head += 0x10u) {
        uint32_t pkg = MR32(mg + head);
        for (int k = 0; pkg && k < 64 && rh_mem_committed(pkg, 0x50u); k++) {
            uint32_t name_ptr = MR32(pkg + 0x0cu);
            if (name_ptr && rh_mem_committed(name_ptr, 16)) {
                const char *nm = (const char *)GPTR(name_ptr);
                if (nm && *nm) {
                    if (strstr(nm, "UI_PC_Help_Bar")) {
                        ms_apply_bar(pkg, s);
                    }
                    ms_apply_arrows(pkg, s);
                }
            }
            pkg = MR32(pkg + 0x04u);
        }
    }
}

void nfs_roam_frame_tick(void) {
    if (!roam_android_init_symbols()) return;
    static int s_perf_bound = 0;
    if (!s_perf_bound) {
        s_perf_bound = 1;
        nfs_bind_to_perf_cores();
    }
    g_frame_count++;
    hiddenraces_frame_tick();
    menusize_frame_tick();
    roam_game_frame(NULL);
}

/* Gamepad Right Stick Quick Chat Input */
void roam_stick_input(float x, float y) {
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
    nfs_roam_frame_tick();
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

    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    __android_log_print(ANDROID_LOG_INFO, "NFSU2-PerfCores", "Bound process to %d performance cores (ret=%d)", count, ret);
    return ret;
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

#ifdef __cplusplus
}
#endif
