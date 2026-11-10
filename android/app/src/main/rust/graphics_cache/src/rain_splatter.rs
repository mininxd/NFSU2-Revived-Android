//! Rain Splatter and Screen Effects Synchronization Engine.
//!
//! Resolves Issue #67 ("Screen distortions and artifacts") when video options
//! like "Rain Splatter" / "raindrops on screen" or intermediate post-process passes are active.
//!
//! Direct3D 9 / DXVK Technical Background:
//! 1. When Rain Splatter is active, SPEED2.EXE copies the backbuffer via StretchRect
//!    into an offscreen render target texture (0x86eb88), binds it as an input texture,
//!    switches render target via SetRenderTarget(0, rt), renders raindrop quads with normal mapping,
//!    and switches render target back to BackBuffer (0x870850).
//! 2. In D3D9 / DXVK, SetRenderTarget(0, rt) automatically overwrites the device viewport
//!    to (0, 0, rt.width, rt.height).
//! 3. The redundant state filter was previously caching the viewport and skipping the game's
//!    restoration SetViewport call, leaving the viewport desynchronized from the active scaled
//!    resolution.
//! 4. Furthermore, on tile-based mobile GPUs (such as Qualcomm Adreno 730 on ROG Phone 6),
//!    texture shadowing across StretchRect and SetRenderTarget passes prevented DXVK from
//!    issuing required Vulkan render-target readback barriers.
//!
//! This module tracks render target transitions, invalidates stale viewport shadows,
//! and ensures complete render target readback synchronization.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

pub struct RainSplatterEngine {
    enabled: AtomicBool,
    in_post_process: AtomicBool,
    last_render_target: AtomicU32,
    rt_switch_count: AtomicU64,
    stretch_rect_count: AtomicU64,
    protected_viewports: AtomicU64,
}

impl RainSplatterEngine {
    pub const fn new() -> Self {
        Self {
            enabled: AtomicBool::new(true),
            in_post_process: AtomicBool::new(false),
            last_render_target: AtomicU32::new(0),
            rt_switch_count: AtomicU64::new(0),
            stretch_rect_count: AtomicU64::new(0),
            protected_viewports: AtomicU64::new(0),
        }
    }

    #[inline]
    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    #[inline]
    pub fn set_enabled(&self, val: bool) {
        self.enabled.store(val, Ordering::Release);
    }

    /// Called when SetRenderTarget is invoked in the D3D9 bridge.
    pub fn notify_render_target(&self, target_surface: u32) {
        self.rt_switch_count.fetch_add(1, Ordering::Relaxed);
        let prev = self.last_render_target.swap(target_surface, Ordering::AcqRel);
        if prev != target_surface {
            self.in_post_process.store(true, Ordering::Release);
        }
        unsafe {
            crate::d3d9_filter::nfs_d3d9_filter_notify_render_target();
        }
    }

    /// Called when StretchRect is invoked in the D3D9 bridge.
    pub fn notify_stretch_rect(&self, _src: u32, _dst: u32) {
        self.stretch_rect_count.fetch_add(1, Ordering::Relaxed);
        self.in_post_process.store(true, Ordering::Release);
        unsafe {
            crate::d3d9_filter::nfs_d3d9_filter_notify_stretch_rect();
        }
    }

    /// Evaluates whether a SetViewport call should be filtered.
    /// Returns true if redundant, false if it MUST be forwarded to DXVK.
    pub fn should_filter_viewport(&self, viewport: &[u32; 6]) -> bool {
        if !self.is_enabled() {
            return false;
        }

        // If a render target or stretch rect recently occurred, force the viewport update
        // through to DXVK so the device viewport matches the game's intended dimensions.
        if self.in_post_process.swap(false, Ordering::AcqRel) {
            self.protected_viewports.fetch_add(1, Ordering::Relaxed);
            unsafe {
                crate::d3d9_filter::nfs_d3d9_filter_viewport_check(viewport.as_ptr());
            }
            return false;
        }

        unsafe {
            crate::d3d9_filter::nfs_d3d9_filter_viewport_check(viewport.as_ptr()) != 0
        }
    }

    pub fn stats(&self, out_rt: *mut u64, out_stretch: *mut u64, out_protected_vp: *mut u64) {
        if !out_rt.is_null() {
            unsafe { *out_rt = self.rt_switch_count.load(Ordering::Relaxed) };
        }
        if !out_stretch.is_null() {
            unsafe { *out_stretch = self.stretch_rect_count.load(Ordering::Relaxed) };
        }
        if !out_protected_vp.is_null() {
            unsafe { *out_protected_vp = self.protected_viewports.load(Ordering::Relaxed) };
        }
    }
}

pub static GLOBAL_RAIN_SPLATTER: RainSplatterEngine = RainSplatterEngine::new();

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_rain_splatter_notify_render_target(target_surface: u32) {
    GLOBAL_RAIN_SPLATTER.notify_render_target(target_surface);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_rain_splatter_notify_stretch_rect(src: u32, dst: u32) {
    GLOBAL_RAIN_SPLATTER.notify_stretch_rect(src, dst);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_rain_splatter_should_filter_viewport(viewport_ptr: *const u32) -> i32 {
    if viewport_ptr.is_null() {
        return 0;
    }
    let slice = std::slice::from_raw_parts(viewport_ptr, 6);
    let mut vp = [0u32; 6];
    vp.copy_from_slice(slice);
    if GLOBAL_RAIN_SPLATTER.should_filter_viewport(&vp) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_rain_splatter_stats(
    out_rt: *mut u64,
    out_stretch: *mut u64,
    out_protected_vp: *mut u64,
) {
    GLOBAL_RAIN_SPLATTER.stats(out_rt, out_stretch, out_protected_vp);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rain_splatter_viewport_protection() {
        let engine = RainSplatterEngine::new();
        let vp = [0, 0, 1280, 720, 0, 1];

        // Before any RT change
        assert!(!engine.in_post_process.load(Ordering::Relaxed));

        // When RT changes, post process flag is raised
        engine.notify_render_target(0x1000);
        assert!(engine.in_post_process.load(Ordering::Relaxed));

        // Next viewport check must NOT be filtered, protecting the game's viewport
        let filtered = engine.should_filter_viewport(&vp);
        assert!(!filtered);
        assert!(!engine.in_post_process.load(Ordering::Relaxed));
    }
}
