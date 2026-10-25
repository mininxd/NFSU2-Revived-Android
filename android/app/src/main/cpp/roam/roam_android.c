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
#include <jni.h>

ptrdiff_t *g_p_mem_base = NULL;
uint32_t  *g_p_esp = NULL;
uint32_t  *g_p_eax = NULL;
uint32_t  *g_p_ecx = NULL;
uint32_t  *g_p_edx = NULL;
recomp_func_t (*g_p_recomp_lookup)(uint32_t va) = NULL;

#define G_CARPOOL_KB 0x007fa9d4u
#define CARPOOL_KB   48000

int roam_android_init_symbols(void) {
    if (g_p_mem_base && g_p_recomp_lookup && g_p_esp && *g_p_mem_base != 0) {
        if (MR32(G_CARPOOL_KB) < CARPOOL_KB) {
            MR32(G_CARPOOL_KB) = CARPOOL_KB;
        }
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
        if (MR32(G_CARPOOL_KB) < CARPOOL_KB) {
            MR32(G_CARPOOL_KB) = CARPOOL_KB;
        }
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
uint32_t roam_plat_crews(uint32_t crews[RC_CREW_MAX], uint32_t rgb[RC_CREW_MAX]) { (void)crews; (void)rgb; return 0; }
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

void nfs_roam_frame_tick(void) {
    if (!roam_android_init_symbols()) return;
    g_frame_count++;
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
        jclass clz = (*env)->FindClass(env, "io/mininxd/nfsu2/AdhocRoamClient");
        if (clz) {
            g_client_class = (jclass)(*env)->NewGlobalRef(env, clz);
            g_mid_on_said = (*env)->GetStaticMethodID(env, g_client_class, "onQuickChatReceived", "(Ljava/lang/String;)V");
            g_mid_on_send = (*env)->GetStaticMethodID(env, g_client_class, "onNativeSend", "(II[B)V");
        }
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamConfigure(
        JNIEnv *env, jclass clazz, jstring jRoom, jstring jName, jstring jPeers, jboolean jGhostMode, jint jHubIp) {
    (void)clazz;
    g_ghost_mode = (jGhostMode == JNI_TRUE) ? 1 : 0;
    g_hub_ip = (uint32_t)jHubIp;
    if (g_s && g_hub_ip) {
        roam_plat_lock();
        g_s->conf.hub_ip = g_hub_ip;
        g_s->conf.hub_port = 47210;
        g_s->conf.snap_states = 1;
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

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamReceive(
        JNIEnv *env, jclass clazz, jint srcIp, jint srcPort, jbyteArray jData) {
    (void)clazz;
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

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamTick(
        JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    nfs_roam_frame_tick();
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamSay(
        JNIEnv *env, jclass clazz, jint phrase) {
    (void)env;
    (void)clazz;
    if (phrase >= 0 && phrase < 16) {
        atomic_store(&g_say_pick, (int)phrase);
    }
}

JNIEXPORT void JNICALL Java_io_mininxd_nfsu2_AdhocRoamClient_nativeRoamStick(
        JNIEnv *env, jclass clazz, jfloat x, jfloat y) {
    (void)env;
    (void)clazz;
    roam_stick_input(x, y);
}

#ifdef __cplusplus
}
#endif
