#include "adpf_manager.h"
#include <dlfcn.h>
#include <android/log.h>

extern "C" {

typedef void (*pfn_adpf_init)(int);
typedef void (*pfn_adpf_void)(void);

static void* get_perf_cores_handle() {
    static void* handle = nullptr;
    if (!handle) {
        handle = dlopen("libperf_cores.so", RTLD_NOLOAD);
        if (!handle) handle = dlopen("libperf_cores.so", RTLD_NOW | RTLD_GLOBAL);
    }
    return handle;
}

void nfs_adpf_init(int target_fps) {
    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_init>(dlsym(h, "nfs_adpf_init")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_init>(dlsym(RTLD_DEFAULT, "nfs_adpf_init"));
    if (fn && fn != &nfs_adpf_init) {
        fn(target_fps);
    }
}

void nfs_adpf_begin_frame(void) {
    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_void>(dlsym(h, "nfs_adpf_begin_frame")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_void>(dlsym(RTLD_DEFAULT, "nfs_adpf_begin_frame"));
    if (fn && fn != &nfs_adpf_begin_frame) {
        fn();
    }
}

void nfs_adpf_end_frame(void) {
    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_void>(dlsym(h, "nfs_adpf_end_frame")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_void>(dlsym(RTLD_DEFAULT, "nfs_adpf_end_frame"));
    if (fn && fn != &nfs_adpf_end_frame) {
        fn();
    }
}

void nfs_adpf_destroy(void) {
    void* h = get_perf_cores_handle();
    auto fn = h ? reinterpret_cast<pfn_adpf_void>(dlsym(h, "nfs_adpf_destroy")) : nullptr;
    if (!fn) fn = reinterpret_cast<pfn_adpf_void>(dlsym(RTLD_DEFAULT, "nfs_adpf_destroy"));
    if (fn && fn != &nfs_adpf_destroy) {
        fn();
    }
}

}
