#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Plan B: Asynchronous Pipeline Cache Disk Persistence
typedef struct PipelineCacheManager PipelineCacheManager;

PipelineCacheManager* nfs_gfx_cache_create(const char* cache_path);
void nfs_gfx_cache_destroy(PipelineCacheManager* mgr);
int nfs_gfx_cache_load(PipelineCacheManager* mgr, uint32_t vendor_id, uint32_t device_id,
                       const uint8_t* uuid, uint8_t** out_data, size_t* out_size);
void nfs_gfx_cache_free_buffer(uint8_t* buf, size_t size);
void nfs_gfx_cache_mark_loaded(PipelineCacheManager* mgr);
int nfs_gfx_cache_save_async(PipelineCacheManager* mgr, const uint8_t* data, size_t size);

// Plan C: Asynchronous Shader & Pipeline Compilation Engine
typedef struct AsyncCompilerEngine AsyncCompilerEngine;

AsyncCompilerEngine* nfs_async_compiler_create(uint32_t worker_count, uint64_t perf_core_mask);
void nfs_async_compiler_destroy(AsyncCompilerEngine* compiler);
int nfs_async_compiler_submit(AsyncCompilerEngine* compiler, uint64_t pipeline_hash,
                             uint32_t priority, const uint8_t* data, size_t size);
int nfs_async_compiler_status(AsyncCompilerEngine* compiler, uint64_t pipeline_hash);
uint64_t nfs_async_compiler_completed_count(AsyncCompilerEngine* compiler);

#ifdef __cplusplus
}
#endif
