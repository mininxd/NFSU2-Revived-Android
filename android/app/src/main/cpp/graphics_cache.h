#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Asynchronous Pipeline Cache Disk Persistence
typedef struct PipelineCacheManager PipelineCacheManager;

PipelineCacheManager* nfs_gfx_cache_create(const char* cache_path);
void nfs_gfx_cache_destroy(PipelineCacheManager* mgr);
int nfs_gfx_cache_load(PipelineCacheManager* mgr, uint32_t vendor_id, uint32_t device_id,
                       const uint8_t* uuid, uint8_t** out_data, size_t* out_size);
void nfs_gfx_cache_free_buffer(uint8_t* buf, size_t size);
void nfs_gfx_cache_mark_loaded(PipelineCacheManager* mgr);
int nfs_gfx_cache_save_async(PipelineCacheManager* mgr, const uint8_t* data, size_t size);

// Asynchronous Shader & Pipeline Compilation Engine
typedef struct AsyncCompilerEngine AsyncCompilerEngine;

AsyncCompilerEngine* nfs_async_compiler_create(uint32_t worker_count, uint64_t perf_core_mask);
void nfs_async_compiler_destroy(AsyncCompilerEngine* compiler);
int nfs_async_compiler_submit(AsyncCompilerEngine* compiler, uint64_t pipeline_hash,
                             uint32_t priority, const uint8_t* data, size_t size);
int nfs_async_compiler_status(AsyncCompilerEngine* compiler, uint64_t pipeline_hash);
uint64_t nfs_async_compiler_completed_count(AsyncCompilerEngine* compiler);

// D3D9 Redundant State Deduplication Filter
int nfs_d3d9_filter_state(uint32_t method, const uint32_t* args, uint32_t* count);
void nfs_d3d9_filter_forget_shadow(void);
void nfs_d3d9_filter_notify_retired(void);
void nfs_d3d9_filter_stats(uint64_t* out_total, uint64_t* out_filtered);

// Decoded DynamicThunk Bytecode Cache & JIT
typedef size_t (*RecompLookupFn)(uint32_t);
typedef uint32_t (*CallLiftedFn)(size_t, uint32_t);

typedef struct ThunkContext {
    uint32_t start_addr;
    uint32_t eax;
    uint32_t ecx;
    uint32_t esi;
    uint32_t esp;
    uint32_t cur_func;
    uint8_t* mem_ptr;
    size_t mem_size;
    RecompLookupFn lookup_fn;
    CallLiftedFn call_fn;
} ThunkContext;

int nfs_dynamic_thunk_run(ThunkContext* ctx);

// Asynchronous Render Command Queue
typedef struct RenderCmd {
    uint16_t op;
    uint16_t method;
    uint32_t args[8];
    size_t data_ptr;
    uint32_t data_size;
    uint32_t sync_id;
} RenderCmd;

void nfs_render_queue_set_enabled(int enabled);
int nfs_render_queue_is_enabled(void);
int nfs_render_queue_push(const RenderCmd* cmd);
int nfs_render_queue_pop(RenderCmd* out_cmd);
void nfs_render_queue_drain(void);
void nfs_render_queue_sync(void);
void nfs_render_queue_stats(uint64_t* out_pushed, uint64_t* out_executed);

// ARM64 NEON SIMD Audio Resampler
void nfs_audio_resample_sinc_simd(
    const float* samples_left,
    const float* samples_right,
    const float* weights_curr,
    const float* weights_next,
    float blend,
    float* out_left,
    float* out_right
);

#ifdef __cplusplus
}
#endif
