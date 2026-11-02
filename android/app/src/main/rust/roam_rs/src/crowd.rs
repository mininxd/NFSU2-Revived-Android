//! Trackside crowd suppression engine for mobile performance optimization.
//!
//! Suppresses 3D spectator models and alpha-tested fence crowds around
//! stadium corners and trackside bleachers to eliminate heavy draw calls,
//! state thrashing, and vertex transform bottlenecks on mobile GPUs.

use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

/// SPEED2.EXE guest memory address for g_CrowdEnable.
pub const G_CROWD_ENABLE_ADDR: u32 = 0x00870CD0;

static CROWD_SUPPRESSION_ENABLED: AtomicBool = AtomicBool::new(true);
static SUPPRESSED_FRAMES: AtomicU32 = AtomicU32::new(0);

/// Check if crowd suppression is globally active in Rust.
#[no_mangle]
pub extern "C" fn nfs_rust_crowd_is_suppressed() -> bool {
    CROWD_SUPPRESSION_ENABLED.load(Ordering::Relaxed)
}

/// Configure crowd suppression state.
#[no_mangle]
pub extern "C" fn nfs_rust_set_crowd_suppression(enable: bool) {
    CROWD_SUPPRESSION_ENABLED.store(enable, Ordering::Relaxed);
}

/// Filter registry or game options value for g_CrowdEnable.
/// Returns 0 if crowd suppression is active, or original value if disabled.
#[no_mangle]
pub extern "C" fn nfs_rust_filter_crowd_setting(val: u32) -> u32 {
    if CROWD_SUPPRESSION_ENABLED.load(Ordering::Relaxed) {
        0
    } else {
        val
    }
}

/// Enforce crowd suppression directly in guest memory at 0x00870CD0 (g_CrowdEnable).
/// Returns true if the memory was successfully inspected and clamped to 0.
#[no_mangle]
pub unsafe extern "C" fn nfs_rust_apply_crowd_suppression(mem_base: *mut u8) -> bool {
    if mem_base.is_null() || !CROWD_SUPPRESSION_ENABLED.load(Ordering::Relaxed) {
        return false;
    }

    let crowd_ptr = mem_base.add(G_CROWD_ENABLE_ADDR as usize) as *mut u32;
    let current = std::ptr::read_volatile(crowd_ptr);
    if current != 0 {
        std::ptr::write_volatile(crowd_ptr, 0);
        SUPPRESSED_FRAMES.fetch_add(1, Ordering::Relaxed);
        true
    } else {
        false
    }
}

/// Return total number of frames where spectator crowds were suppressed.
#[no_mangle]
pub extern "C" fn nfs_rust_crowd_suppressed_frames() -> u32 {
    SUPPRESSED_FRAMES.load(Ordering::Relaxed)
}
