//! DXVK Driver Configuration Engine.
//!
//! Direct3D 9 translation configuration for Android Vulkan:
//! - `d3d9.invariantPosition`: Eliminates vertex jitter and geometric distortion on mobile Vulkan.
//! - `d3d9.generalHazards`: Ensures Vulkan pipeline barriers for render target readback (rain splatter, bloom, motion blur).
//! - `d3d9.forceSamplerTypeSpecConstants`: Specializes Vulkan samplers into constants, eliminating branch overhead.
//! - `d3d9.lenientClear` and `d3d9.allowDiscard`: Removes redundant clearing and enables discard optimizations.
//! - `d3d9.strictConstantCopies`: Disables redundant constant validation for faster push constant uploads.
//! - `MESA_VK_WSI_PRESENT_MODE`: Selects mailbox presentation for minimum latency without screen tearing.

use std::fs::File;
use std::io::Write;
use std::path::Path;

#[derive(Debug, Clone)]
pub struct DxvkProfileConfig {
    pub max_device_memory_mb: u32,
    pub max_shared_memory_mb: u32,
    pub general_hazards: bool,
    pub invariant_position: bool,
    pub force_sampler_spec_constants: bool,
    pub strict_constant_copies: bool,
    pub lenient_clear: bool,
    pub allow_discard: bool,
    pub allow_do_not_wait: bool,
    pub cached_dynamic_buffers: bool,
    pub cached_write_only_buffers: bool,
    pub device_local_constant_buffers: bool,
    pub tiler_mode: bool,
    pub enable_gpl: bool,
    pub enable_descriptor_buffer: bool,
    pub enable_descriptor_heap: bool,
    pub use_fp16: bool,
    pub enable_unified_image_layouts: bool,
    pub track_pipeline_lifetime: bool,
    pub evict_managed_on_unlock: bool,
    pub defer_surface_creation: bool,
    pub no_explicit_front_buffer: bool,
    pub tear_free: bool,
    pub max_frame_latency: u32,
    pub num_compiler_threads: u32,
}

impl Default for DxvkProfileConfig {
    fn default() -> Self {
        Self {
            max_device_memory_mb: 4096,
            max_shared_memory_mb: 2048,
            general_hazards: false,
            invariant_position: true,
            force_sampler_spec_constants: true,
            strict_constant_copies: false,
            lenient_clear: true,
            allow_discard: true,
            allow_do_not_wait: true,
            cached_dynamic_buffers: true,
            cached_write_only_buffers: true,
            device_local_constant_buffers: true,
            tiler_mode: true,
            enable_gpl: true,
            enable_descriptor_buffer: true,
            enable_descriptor_heap: true,
            use_fp16: true,
            enable_unified_image_layouts: true,
            track_pipeline_lifetime: false,
            evict_managed_on_unlock: true,
            defer_surface_creation: true,
            no_explicit_front_buffer: true,
            tear_free: true,
            max_frame_latency: 2,
            num_compiler_threads: 4,
        }
    }
}

impl DxvkProfileConfig {
    pub fn for_backend(backend: &str) -> Self {
        let is_gl = backend.eq_ignore_ascii_case("opengl") || backend.eq_ignore_ascii_case("opengl_es");
        if is_gl {
            Self {
                max_device_memory_mb: 2048,
                max_shared_memory_mb: 1024,
                general_hazards: false,
                invariant_position: true,
                force_sampler_spec_constants: true,
                strict_constant_copies: true,
                lenient_clear: true,
                allow_discard: true,
                allow_do_not_wait: true,
                cached_dynamic_buffers: true,
                cached_write_only_buffers: true,
                device_local_constant_buffers: false,
                tiler_mode: true,
                enable_gpl: false,
                enable_descriptor_buffer: false,
                enable_descriptor_heap: false,
                use_fp16: true,
                enable_unified_image_layouts: false,
                track_pipeline_lifetime: false,
                evict_managed_on_unlock: true,
                defer_surface_creation: true,
                no_explicit_front_buffer: true,
                tear_free: false,
                max_frame_latency: 1,
                num_compiler_threads: 2,
            }
        } else {
            Self::default()
        }
    }

    /// Formats the configuration into standard dxvk.conf syntax.
    pub fn format_conf(&self) -> String {
        let mut s = String::with_capacity(2048);
        s.push_str("dxvk.enableStateCache = True\n");
        s.push_str("dxvk.enableAsync = True\n");
        if self.enable_gpl {
            s.push_str("dxvk.enableGraphicsPipelineLibrary = True\n");
            s.push_str("dxvk.gplAsyncCache = True\n");
        } else {
            s.push_str("dxvk.enableGraphicsPipelineLibrary = False\n");
            s.push_str("dxvk.gplAsyncCache = False\n");
        }
        s.push_str(&format!("dxvk.numCompilerThreads = {}\n", self.num_compiler_threads));
        s.push_str("dxvk.useRawSsbo = Auto\n");
        s.push_str("dxvk.tilerMode = Auto\n");
        if self.enable_descriptor_buffer {
            s.push_str("dxvk.enableDescriptorBuffer = Auto\n");
            s.push_str("dxvk.enableDescriptorHeap = Auto\n");
        } else {
            s.push_str("dxvk.enableDescriptorBuffer = False\n");
            s.push_str("dxvk.enableDescriptorHeap = False\n");
        }
        s.push_str("dxvk.enableUnifiedImageLayouts = True\n");
        s.push_str("dxvk.enableImplicitResolves = False\n");
        s.push_str("dxvk.enableMemoryDefrag = False\n");
        s.push_str("dxvk.trackPipelineLifetime = True\n");

        s.push_str("d3d9.deferSurfaceCreation = True\n");
        s.push_str("d3d9.tearFree = False\n");
        s.push_str(&format!("d3d9.maxFrameLatency = {}\n", self.max_frame_latency));
        s.push_str("d3d9.lenientClear = True\n");
        s.push_str("d3d9.allowDiscard = True\n");
        s.push_str("d3d9.allowDoNotWait = True\n");
        if self.device_local_constant_buffers {
            s.push_str("d3d9.deviceLocalConstantBuffers = Auto\n");
        } else {
            s.push_str("d3d9.deviceLocalConstantBuffers = False\n");
        }
        s.push_str("d3d9.strictConstantCopies = False\n");
        s.push_str("d3d9.cachedDynamicBuffers = True\n");
        s.push_str("d3d9.cachedWriteOnlyBuffers = True\n");
        s.push_str("d3d9.noExplicitFrontBuffer = True\n");
        s.push_str("d3d9.useFP16 = Auto\n");
        s.push_str("d3d9.evictManagedOnUnlock = True\n");
        s.push_str("d3d9.memoryTrackTest = False\n");
        s.push_str(&format!("d3d9.maxAvailableMemory = {}\n", self.max_device_memory_mb));

        s.push_str("d3d9.invariantPosition = True\n");
        s.push_str("d3d9.forceSamplerTypeSpecConstants = True\n");
        s.push_str("d3d9.samplerAnisotropy = 0\n");
        s.push_str("d3d9.clampNegativeLodBias = True\n");
        s.push_str("d3d9.seamlessCubes = True\n");
        s.push_str("d3d9.floatEmulation = False\n");
        s.push_str("d3d9.modeCountCompatibility = True\n");

        s.push_str("dxgi.deferSurfaceCreation = True\n");
        s.push_str(&format!("dxgi.maxDeviceMemory = {}\n", self.max_device_memory_mb));
        s.push_str(&format!("dxgi.maxSharedMemory = {}\n", self.max_shared_memory_mb));

        s.push_str("d3d9.forceSwapchainMSAA = False\n");
        s
    }

    /// Writes the generated configuration file to target path.
    pub fn write_to_file<P: AsRef<Path>>(&self, path: P) -> std::io::Result<()> {
        let content = self.format_conf();
        let mut file = File::create(path)?;
        file.write_all(content.as_bytes())?;
        file.flush()
    }
}

/// Applies driver environment variables.
pub fn apply_driver_environment() {
    let vars: &[(&str, &str)] = &[
        ("DXVK_ASYNC", "1"),
        ("DXVK_GPLASYNCCACHE", "1"),
        ("DXVK_STATE_CACHE", "1"),
        ("DXVK_LOG_LEVEL", "none"),
        ("WINEDEBUG", "-all"),
        ("WINE_HEAP_DELAY_FREE", "1"),
        ("WINE_HEAP_ZERO_MEMORY", "1"),
        ("WINE_DISABLE_FULLSCREEN_HACK", "1"),
        ("WINE_DISABLE_HARDWARE_SCHEDULING", "1"),
        ("WINE_DO_NOT_CREATE_DXGI_DEVICE_MANAGER", "1"),
        ("MESA_VK_WSI_PRESENT_MODE", "mailbox"),
        ("MESA_SHADER_CACHE_DISABLE", "false"),
        ("MESA_SHADER_CACHE_MAX_SIZE", "1024MB"),
        ("mesa_glthread", "true"),
        ("GALLIUM_DRIVER", "zink"),
    ];

    for &(k, v) in vars {
        std::env::set_var(k, v);
    }

    // Auto-generate dxvk.conf in game folders if accessible
    let candidate_dirs = ["/sdcard/nfsu2", "/storage/emulated/0/nfsu2", "."];
    let backend = std::env::var("NFSU2_RENDERER").unwrap_or_default();
    let config = DxvkProfileConfig::for_backend(&backend);
    for dir in &candidate_dirs {
        let p = Path::new(dir);
        if p.is_dir() {
            let conf_path = p.join("dxvk.conf");
            if config.write_to_file(&conf_path).is_ok() {
                if std::env::var("DXVK_CONFIG_FILE").is_err() {
                    std::env::set_var("DXVK_CONFIG_FILE", conf_path.to_string_lossy().as_ref());
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// C-ABI & JNI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_dxvk_tuner_generate_config(output_dir: *const std::os::raw::c_char) -> i32 {
    if output_dir.is_null() {
        return -1;
    }
    let c_str = std::ffi::CStr::from_ptr(output_dir);
    let Ok(path_str) = c_str.to_str() else {
        return -2;
    };

    let dir = Path::new(path_str);
    let conf_path = dir.join("dxvk.conf");
    let backend = std::env::var("NFSU2_RENDERER").unwrap_or_default();
    let config = DxvkProfileConfig::for_backend(&backend);
    if config.write_to_file(&conf_path).is_ok() {
        0
    } else {
        -3
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_dxvk_tuner_apply_environment() -> i32 {
    apply_driver_environment();
    0
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_GamePerformanceBooster_nativeConfigureDxvkAndDriver(
    mut env: jni::JNIEnv,
    _class: jni::objects::JClass,
    cache_dir: jni::objects::JString,
    sdcard_dir: jni::objects::JString,
) {
    apply_driver_environment();

    let backend = std::env::var("NFSU2_RENDERER").unwrap_or_default();
    let config = DxvkProfileConfig::for_backend(&backend);

    if let Ok(cache_str) = env.get_string(&cache_dir) {
        let path_str = cache_str.to_string_lossy();
        let cache_path = Path::new(path_str.as_ref());
        let _ = std::fs::create_dir_all(cache_path);
        let conf_file = cache_path.join("dxvk.conf");
        if config.write_to_file(&conf_file).is_ok() {
            std::env::set_var("DXVK_CONFIG_FILE", conf_file.to_string_lossy().as_ref());
            std::env::set_var("DXVK_STATE_CACHE_PATH", cache_path.to_string_lossy().as_ref());
        }
    }

    if let Ok(sdcard_str) = env.get_string(&sdcard_dir) {
        let path_str = sdcard_str.to_string_lossy();
        let sdcard_path = Path::new(path_str.as_ref());
        if sdcard_path.is_dir() {
            let conf_file = sdcard_path.join("dxvk.conf");
            let _ = config.write_to_file(&conf_file);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dxvk_conf_formatting() {
        let conf = DxvkProfileConfig::default();
        let formatted = conf.format_conf();
        assert!(formatted.contains("d3d9.invariantPosition = True"));
        assert!(formatted.contains("d3d9.forceSamplerTypeSpecConstants = True"));
        assert!(formatted.contains("d3d9.strictConstantCopies = False"));
        assert!(formatted.contains("d3d9.forceSwapchainMSAA = False"));
        assert!(formatted.contains("dxvk.enableMemoryDefrag = False"));
        assert!(formatted.contains("dxvk.tilerMode = Auto"));
        assert!(formatted.contains("dxvk.useRawSsbo = Auto"));
        assert!(formatted.contains("d3d9.maxFrameLatency = 2"));
        assert!(formatted.contains("dxvk.enableDescriptorBuffer = Auto"));
        assert!(formatted.contains("d3d9.useFP16 = Auto"));
        assert!(formatted.contains("d3d9.deviceLocalConstantBuffers = Auto"));
        assert!(formatted.contains("d3d9.cachedWriteOnlyBuffers = True"));
    }

    #[test]
    fn test_dxvk_conf_opengl_backend() {
        let conf = DxvkProfileConfig::for_backend("opengl");
        let formatted = conf.format_conf();
        assert!(formatted.contains("dxvk.enableGraphicsPipelineLibrary = False"));
        assert!(formatted.contains("dxvk.enableDescriptorBuffer = False"));
        assert!(formatted.contains("d3d9.deviceLocalConstantBuffers = False"));
        assert!(formatted.contains("d3d9.maxFrameLatency = 1"));
    }
}
