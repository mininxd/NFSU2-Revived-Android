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

// OpenGL / GLES Rendering Optimizer
int nfs_gl_filter_enable(uint32_t cap_idx, int enabled);
int nfs_gl_filter_blend_func(uint32_t src_rgb, uint32_t dst_rgb, uint32_t src_a, uint32_t dst_a);
int nfs_gl_filter_depth_mask(uint32_t flag);
int nfs_gl_filter_depth_func(uint32_t func);
int nfs_gl_filter_bind_texture(uint32_t unit, uint32_t texture);
int nfs_gl_filter_use_program(uint32_t program);
int nfs_gl_filter_bind_buffer(uint32_t target, uint32_t buffer);
int nfs_gl_filter_viewport(int x, int y, int w, int h);
void nfs_gl_filter_invalidate(void);

int nfs_dxt_decode_rgba8(uint32_t mode, uint32_t width, uint32_t height,
                         const uint8_t* raw, size_t raw_size, uint8_t* out_rgba);

void nfs_gl_program_cache_init(const char* cache_dir);
int nfs_gl_program_cache_load(uint64_t shader_hash, uint8_t** out_binary,
                              size_t* out_size, uint32_t* out_format);
int nfs_gl_program_cache_save(uint64_t shader_hash, uint32_t binary_format,
                              const uint8_t* data, size_t size);
void nfs_gl_program_cache_free_buffer(uint8_t* buf, size_t size);

int nfs_gl_vbo_ring_alloc(size_t bytes, size_t* out_offset);
void nfs_gl_vbo_ring_reset(void);

#ifdef __cplusplus
}
#endif
