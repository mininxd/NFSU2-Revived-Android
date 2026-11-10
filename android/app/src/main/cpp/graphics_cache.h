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
int nfs_d3d9_filter_transform(uint32_t state, const float* matrix);
int nfs_d3d9_filter_viewport(const uint32_t* viewport);
void nfs_d3d9_filter_forget_shadow(void);
void nfs_d3d9_filter_notify_retired(void);
void nfs_d3d9_filter_notify_render_target(void);
void nfs_d3d9_filter_notify_stretch_rect(void);
void nfs_d3d9_filter_stats(uint64_t* out_total, uint64_t* out_filtered);

// Rain Splatter & Screen Effects Synchronization Engine (Issue #67)
void nfs_rain_splatter_notify_render_target(uint32_t target_surface);
void nfs_rain_splatter_notify_stretch_rect(uint32_t src, uint32_t dst);
int nfs_rain_splatter_should_filter_viewport(const uint32_t* viewport);
void nfs_rain_splatter_reset(void);
void nfs_rain_splatter_stats(uint64_t* out_rt, uint64_t* out_stretch, uint64_t* out_protected_vp);

// DXVK & Wine Driver Performance Tuning Engine
int nfs_dxvk_tuner_generate_config(const char* output_dir);
int nfs_dxvk_tuner_apply_environment(void);

// Right Stick Camera Orbit & PS2 Camera Rotation Engine
void nfs_camera_stick_input(float x, float y);
void nfs_camera_stick_apply_frame(void* guest_base);
void nfs_camera_stick_restore_frame(void* guest_base);
int nfs_camera_stick_transform_view_matrix(float* matrix, const void* guest_base);
void nfs_camera_stick_set_enabled(int enabled);
float nfs_camera_stick_get_yaw(void);

// Dynamic Camera Forward-Only Back-Culling Engine
void nfs_camera_cull_set_enabled(int enabled);
int nfs_camera_cull_is_enabled(void);
void nfs_camera_cull_set_margin(float margin);
void nfs_camera_cull_set_far_distance(float dist);
void nfs_camera_cull_update_camera(float pos_x, float pos_y, float pos_z,
                                   float fwd_x, float fwd_y, float fwd_z);
void nfs_camera_cull_update_view(const float* view_ptr);
void nfs_camera_cull_update_proj(const float* proj_ptr);
void nfs_camera_cull_update_world(const float* world_ptr);
void nfs_camera_cull_set_screen_space(int is_screen_space);
int nfs_camera_cull_test_draw(void);
void nfs_camera_cull_stats(uint64_t* out_total, uint64_t* out_culled);
int nfs_camera_cull_get_camera(float* out_pos, float* out_fwd);

// Direct3D 9 Vertex Declaration & FVF Decomposition
typedef struct D3DVertexElement9Raw {
    uint16_t stream;
    uint16_t offset;
    uint8_t element_type;
    uint8_t method;
    uint8_t usage;
    uint8_t usage_index;
} D3DVertexElement9Raw;

uint32_t nfs_d3d9_get_stream_stride(const D3DVertexElement9Raw* elements, uint32_t stream);
uint64_t nfs_d3d9_hash_vertex_declaration(const D3DVertexElement9Raw* elements);
uint32_t nfs_d3d9_fvf_to_declaration(uint32_t fvf, D3DVertexElement9Raw* out_elements, uint32_t max_count);

// Audio Engine Batch Voice Advancement
typedef struct VoiceAdvanceState {
    double position;
    double stale_frames;
    double step;
    size_t count;
    uint32_t loop_mode;
    uint32_t is_streaming;
    uint32_t is_playing;
} VoiceAdvanceState;

void nfs_audio_mixer_advance_voices(VoiceAdvanceState* voices, size_t voice_count, uint32_t frames);

// DirectSound Buffer Cursor & Lock Calculations
void nfs_dsound_calc_cursors(double position_frames, uint32_t block_align, uint32_t total_bytes, uint32_t sample_rate, uint32_t* out_play, uint32_t* out_write);
int nfs_dsound_calc_lock_slices(uint32_t total_bytes, uint32_t offset, uint32_t length, uint32_t flags, uint32_t current_play_bytes, uint32_t sample_rate, uint32_t block_align, uint32_t* out_offset, uint32_t* out_first_bytes, uint32_t* out_second_bytes);
void nfs_dsound_calc_volume_pan_gains(int32_t volume_m_b, int32_t pan_m_b, float* out_left, float* out_right);

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

// ARM64 NEON SIMD Audio Resampler & Vectorized Voice Mixer
typedef struct VoiceDesc {
    const uint8_t* samples;
    uint32_t bits;
    uint32_t channels;
    size_t total_frames;
    double position;
    double step;
    int32_t loop_mode;
    float gain_l;
    float gain_r;
} VoiceDesc;

int nfs_audio_mixer_render_voice(
    float* output,
    uint32_t frames,
    const VoiceDesc* desc,
    const float* kernel_weights,
    uint32_t num_phases,
    uint32_t num_taps,
    double* out_pos
);

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
int nfs_tex_dxt1_decode_fast(uint32_t width, uint32_t height, const uint8_t* src, size_t src_len, uint8_t* dst_rgba);
int nfs_tex_dxt3_decode_fast(uint32_t width, uint32_t height, const uint8_t* src, size_t src_len, uint8_t* dst_rgba);
int nfs_tex_dxt5_decode_fast(uint32_t width, uint32_t height, const uint8_t* src, size_t src_len, uint8_t* dst_rgba);

void nfs_gl_program_cache_init(const char* cache_dir);
int nfs_gl_program_cache_load(uint64_t shader_hash, uint8_t** out_binary,
                              size_t* out_size, uint32_t* out_format);
int nfs_gl_program_cache_save(uint64_t shader_hash, uint32_t binary_format,
                              const uint8_t* data, size_t size);
void nfs_gl_program_cache_free_buffer(uint8_t* buf, size_t size);

int nfs_gl_vbo_ring_alloc(size_t bytes, size_t* out_offset);
void nfs_gl_vbo_ring_reset(void);

// High-Performance Rust Guest Memory Heap Manager
int rust_guest_heap_init(uint32_t first_arena, uint32_t first_arena_size, uint32_t second_arena, uint32_t second_arena_size);
uint32_t rust_guest_heap_create(uint32_t options, uint32_t initial, uint32_t maximum);
int rust_guest_heap_destroy(uint32_t heap_id);
uint32_t rust_guest_heap_alloc(uint32_t heap_id, uint32_t flags, uint32_t bytes, uint32_t* out_commit_cap, uint32_t* out_commit_slack, int* out_exception);
int rust_guest_heap_free(uint32_t heap_id, uint32_t address);
uint32_t rust_guest_heap_size(uint32_t heap_id, uint32_t address);
uint32_t rust_guest_heap_realloc(uint32_t heap_id, uint32_t flags, uint32_t address, uint32_t bytes, uint32_t* out_commit_cap, uint32_t* out_commit_slack, uint32_t* out_old_requested, int* out_is_in_place);

#ifdef __cplusplus
}
#endif
