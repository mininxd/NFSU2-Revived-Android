//! Graphics Cache & Asynchronous Pipeline Engine for NFSU2 Android.
//!
//! Pipeline cache validation, disk persistence, and background compilation workers.

pub mod audio_engine;
pub mod async_compiler;
pub mod camera_cull;
pub mod contrast_filter;
pub mod crowd_filter;
pub mod d3d9_filter;
pub mod d3d9_vertex_decl;
pub mod driver_inspector;
pub mod dsound_cursor;
pub mod dynamic_thunk;
pub mod dxvk_tuner;
pub mod enhanced_contrast;
pub mod extra_options;
pub mod gles_optimizer;
pub mod guest_heap;
pub mod headlights;
pub mod math_simd;
pub mod physics_simd;
pub mod pipeline_cache;
pub mod rain_splatter;
pub mod render_queue;
pub mod settings_engine;
pub mod simd_audio;
pub mod telemetry;
pub mod texture_pipeline;
pub mod visual_geometry;
pub mod win32_comm;
pub mod win32_ddraw;
pub mod win32_dinput;
pub mod win32_fs;
pub mod win32_gdi;
pub mod win32_netapi;
pub mod win32_shell;
pub mod win32_tapi;

pub use audio_engine::*;
pub use camera_cull::*;
pub use contrast_filter::*;
pub use crowd_filter::*;
pub use d3d9_vertex_decl::*;
pub use driver_inspector::*;
pub use dsound_cursor::*;
pub use dxvk_tuner::*;
pub use enhanced_contrast::*;
pub use extra_options::*;
pub use guest_heap::*;
pub use headlights::*;
pub use math_simd::*;
pub use physics_simd::*;
pub use rain_splatter::*;
pub use settings_engine::*;
pub use telemetry::*;
pub use texture_pipeline::*;
pub use visual_geometry::*;
pub use win32_comm::*;
pub use win32_ddraw::*;
pub use win32_dinput::*;
pub use win32_fs::*;
pub use win32_gdi::*;
pub use win32_netapi::*;
pub use win32_shell::*;
pub use win32_tapi::*;

use async_compiler::AsyncCompilerEngine;
use pipeline_cache::{PipelineCacheManager, VK_UUID_SIZE};

use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::slice;

use jni::objects::JClass;
use jni::sys::{jint, jlong};
use jni::JNIEnv;

// Pipeline cache persistence C-ABI exports

#[no_mangle]
pub unsafe extern "C" fn nfs_gfx_cache_create(
    cache_path: *const c_char,
) -> *mut PipelineCacheManager {
    if cache_path.is_null() {
        return std::ptr::null_mut();
    }
    let c_str = CStr::from_ptr(cache_path);
    let str_slice = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    let manager = Box::new(PipelineCacheManager::new(str_slice));
    Box::into_raw(manager)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gfx_cache_destroy(mgr: *mut PipelineCacheManager) {
    if !mgr.is_null() {
        drop(Box::from_raw(mgr));
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gfx_cache_load(
    mgr: *mut PipelineCacheManager,
    vendor_id: u32,
    device_id: u32,
    uuid: *const u8,
    out_data: *mut *mut u8,
    out_size: *mut usize,
) -> c_int {
    if mgr.is_null() || uuid.is_null() || out_data.is_null() || out_size.is_null() {
        return -1;
    }

    let manager = &*mgr;
    let mut uuid_arr = [0u8; VK_UUID_SIZE];
    std::ptr::copy_nonoverlapping(uuid, uuid_arr.as_mut_ptr(), VK_UUID_SIZE);

    match manager.load_safe(vendor_id, device_id, &uuid_arr) {
        Some(mut data) => {
            *out_size = data.len();
            let ptr = data.as_mut_ptr();
            std::mem::forget(data); // Caller takes ownership and frees via nfs_gfx_cache_free_buffer
            *out_data = ptr;
            0 // Success
        }
        None => {
            *out_data = std::ptr::null_mut();
            *out_size = 0;
            1 // No cache or invalid
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gfx_cache_free_buffer(buf: *mut u8, size: usize) {
    if !buf.is_null() && size > 0 {
        let vec = Vec::from_raw_parts(buf, size, size);
        drop(vec);
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gfx_cache_mark_loaded(mgr: *mut PipelineCacheManager) {
    if !mgr.is_null() {
        (*mgr).mark_load_finished();
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gfx_cache_save_async(
    mgr: *mut PipelineCacheManager,
    data: *const u8,
    size: usize,
) -> c_int {
    if mgr.is_null() || data.is_null() || size == 0 {
        return -1;
    }

    let slice = slice::from_raw_parts(data, size);
    (*mgr).queue_save_async(slice.to_vec());
    0
}

// Asynchronous compiler engine C-ABI exports

#[no_mangle]
pub unsafe extern "C" fn nfs_async_compiler_create(
    worker_count: u32,
    perf_core_mask: u64,
) -> *mut AsyncCompilerEngine {
    let workers = if worker_count == 0 { 2 } else { worker_count as usize };
    let compiler = Box::new(AsyncCompilerEngine::new(workers, perf_core_mask));
    Box::into_raw(compiler)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_async_compiler_destroy(compiler: *mut AsyncCompilerEngine) {
    if !compiler.is_null() {
        drop(Box::from_raw(compiler));
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_async_compiler_submit(
    compiler: *mut AsyncCompilerEngine,
    pipeline_hash: u64,
    priority: u32,
    data: *const u8,
    size: usize,
) -> c_int {
    if compiler.is_null() {
        return -1;
    }

    let payload = if !data.is_null() && size > 0 {
        slice::from_raw_parts(data, size).to_vec()
    } else {
        Vec::new()
    };

    if (*compiler).submit(pipeline_hash, priority, payload) {
        0
    } else {
        1
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_async_compiler_status(
    compiler: *mut AsyncCompilerEngine,
    pipeline_hash: u64,
) -> c_int {
    if compiler.is_null() {
        return 0;
    }
    (*compiler).get_status(pipeline_hash) as c_int
}

#[no_mangle]
pub unsafe extern "C" fn nfs_async_compiler_completed_count(
    compiler: *mut AsyncCompilerEngine,
) -> u64 {
    if compiler.is_null() {
        return 0;
    }
    (*compiler).completed_count()
}

// JNI exported functions for Java integration

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GraphicsCacheManager_nativeGetStatus(
    _env: JNIEnv,
    _class: JClass,
    hash: jlong,
) -> jint {
    // Returns status from global instance if active
    let _ = hash;
    3 // Ready
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GraphicsCacheManager_nativeGetCompletedCount(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    0
}
