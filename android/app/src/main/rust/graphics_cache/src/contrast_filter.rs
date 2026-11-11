//! Pure Rust Enhanced Contrast and Tone Mapping Regulator for NFSU2 Android.
//!
//! Controls Enhanced Contrast (Bleach Bypass vs OverBright Bloom) post-processing passes.
//! On mobile Adreno GPUs (e.g. Snapdragon 8 Elite / Adreno 840, Snapdragon 8 Gen 2 / Adreno 740),
//! running both multi-pass OverBright bloom and Bleach Bypass generates 6+ render target resolves
//! and heavy pipeline barriers per frame, causing FPS to drop from 60 to 35-45 (Issue #70).
//!
//! Modes:
//! - 0 = Off (Fastest): Both OverBright and Bleach Bypass disabled.
//! - 1 = Optimized (Smooth 60 FPS): Bleach Bypass enabled for vivid high-contrast aesthetic,
//!       OverBright bloom disabled to eliminate heavy TBDR tile resolves and frame hitching.
//! - 2 = Original (High GPU Load): Both OverBright and Bleach Bypass enabled (PC original behavior).

use std::sync::atomic::{AtomicU32, Ordering};
use jni::JNIEnv;
use jni::objects::JClass;
use jni::sys::jint;

pub const CONTRAST_MODE_OFF: u32 = 0;
pub const CONTRAST_MODE_OPTIMIZED: u32 = 1;
pub const CONTRAST_MODE_ORIGINAL: u32 = 2;

static CONTRAST_MODE: AtomicU32 = AtomicU32::new(CONTRAST_MODE_OPTIMIZED);

/// Pure Rust C-ABI setter: sets contrast regulation mode.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_set_contrast_mode(mode: u32) {
    CONTRAST_MODE.store(mode, Ordering::Relaxed);
}

/// Pure Rust C-ABI getter: gets active contrast regulation mode.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_get_contrast_mode() -> u32 {
    CONTRAST_MODE.load(Ordering::Relaxed)
}

/// Filter registry or option value for g_OverBrightEnable (address 0x00870CFC).
/// Returns 1 only in Original mode (2); returns 0 in Optimized (1) and Off (0).
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_filter_overbright(default_val: u32) -> u32 {
    match CONTRAST_MODE.load(Ordering::Relaxed) {
        CONTRAST_MODE_OFF | CONTRAST_MODE_OPTIMIZED => 0,
        CONTRAST_MODE_ORIGINAL => default_val.max(1),
        _ => 0,
    }
}

/// Filter registry or option value for g_BleachByPassEnable (address 0x00870D00).
/// Returns 1 in Optimized (1) and Original (2); returns 0 in Off (0).
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_filter_bleach_bypass(default_val: u32) -> u32 {
    match CONTRAST_MODE.load(Ordering::Relaxed) {
        CONTRAST_MODE_OFF => 0,
        CONTRAST_MODE_OPTIMIZED | CONTRAST_MODE_ORIGINAL => default_val.max(1),
        _ => default_val,
    }
}

// JNI bindings for GraphicsCacheManager

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GraphicsCacheManager_nativeSetContrastMode(
    _env: JNIEnv,
    _class: JClass,
    mode: jint,
) {
    CONTRAST_MODE.store(mode as u32, Ordering::Relaxed);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GraphicsCacheManager_nativeGetContrastMode(
    _env: JNIEnv,
    _class: JClass,
) -> jint {
    CONTRAST_MODE.load(Ordering::Relaxed) as jint
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_contrast_filter_modes() {
        nfs_gfx_rust_set_contrast_mode(CONTRAST_MODE_OFF);
        assert_eq!(nfs_gfx_rust_get_contrast_mode(), 0);
        assert_eq!(nfs_gfx_rust_filter_overbright(1), 0);
        assert_eq!(nfs_gfx_rust_filter_bleach_bypass(1), 0);

        nfs_gfx_rust_set_contrast_mode(CONTRAST_MODE_OPTIMIZED);
        assert_eq!(nfs_gfx_rust_get_contrast_mode(), 1);
        assert_eq!(nfs_gfx_rust_filter_overbright(1), 0);
        assert_eq!(nfs_gfx_rust_filter_bleach_bypass(1), 1);

        nfs_gfx_rust_set_contrast_mode(CONTRAST_MODE_ORIGINAL);
        assert_eq!(nfs_gfx_rust_get_contrast_mode(), 2);
        assert_eq!(nfs_gfx_rust_filter_overbright(1), 1);
        assert_eq!(nfs_gfx_rust_filter_bleach_bypass(1), 1);
    }
}
