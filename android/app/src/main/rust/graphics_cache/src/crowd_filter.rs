//! Trackside crowd suppression filter for graphics cache and Vulkan pipeline management.
//!
//! Exposes pure Rust configuration and JNI hooks to control trackside crowd
//! spectator culling across Java, guest runtime, and Vulkan render loops.

use std::sync::atomic::{AtomicBool, Ordering};
use jni::JNIEnv;
use jni::objects::JClass;
use jni::sys::jboolean;

static CROWD_SUPPRESSED: AtomicBool = AtomicBool::new(true);

/// Pure Rust C-ABI query: is crowd suppression enabled?
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_crowd_is_suppressed() -> bool {
    CROWD_SUPPRESSED.load(Ordering::Relaxed)
}

/// Pure Rust C-ABI setter: enable or disable crowd suppression.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_set_crowd_suppression(suppressed: bool) {
    CROWD_SUPPRESSED.store(suppressed, Ordering::Relaxed);
}

/// Filter registry or option value for g_CrowdEnable.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_filter_crowd(val: u32) -> u32 {
    if CROWD_SUPPRESSED.load(Ordering::Relaxed) {
        0
    } else {
        val
    }
}

// JNI bindings for GraphicsCacheManager

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GraphicsCacheManager_nativeSetCrowdSuppression(
    _env: JNIEnv,
    _class: JClass,
    suppressed: jboolean,
) {
    CROWD_SUPPRESSED.store(suppressed != 0, Ordering::Relaxed);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GraphicsCacheManager_nativeIsCrowdSuppressed(
    _env: JNIEnv,
    _class: JClass,
) -> jboolean {
    if CROWD_SUPPRESSED.load(Ordering::Relaxed) {
        1
    } else {
        0
    }
}
