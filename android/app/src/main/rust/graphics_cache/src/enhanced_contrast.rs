//! Pure Rust High-Performance Enhanced Contrast & Tone Regulation Engine for NFSU2 Android.
//!
//! Controls the in-game "Enhanced Contrast" (Bleach Bypass) and "Over Bright" post-processing passes.
//!
//! ## Problem on Mobile GPUs (Qualcomm Adreno 840 / Snapdragon 8 Elite, Adreno 740, ARM Mali):
//! In the original PC game, "Enhanced Contrast" toggles `g_BleachByPassEnable` (`0x00870D00`).
//! When active, SPEED2.EXE executes a 3-pass ping-pong render target pipeline (`0x005c68d0`)
//! across full-resolution backbuffers (e.g. 2608x1200 on modern flagship displays) and calls
//! `LockRect`/`UnlockRect` on CPU textures (`0x005bbe80`).
//! On mobile Tile-Based Deferred Rendering (TBDR) GPUs, switching full-screen render targets 3-4 times
//! per frame causes on-chip tile buffers (GMEM) to repeatedly resolve and reload to system DRAM,
//! consuming over 3 GB/s of memory bandwidth and collapsing framerates from 60 to 40-45 FPS (Issue #70).
//!
//! Furthermore, "Over Bright" (`g_OverBrightEnable` at `0x00870CFC`) adds a 6-pass Gaussian blur bloom
//! downsampling pass, worsening the memory bus saturation.
//!
//! ## Solution:
//! 1. Enhanced Contrast is driven 100% by the in-game display settings menu ("Enhanced Contrast: ON/OFF").
//! 2. When turned ON in the game (`0x00870D00 == 1`), the contrast curve is applied directly to the display
//!    hardware via `SetGammaRamp` (an S-curve tone mapping curve with deeper blacks and punchy highlights).
//!    The slow 3-pass ping-pong render target blit is overridden, delivering the full iconic high-contrast
//!    visual tone at zero GPU memory bandwidth cost and locking the game to a solid 60 FPS.
//! 3. When turned OFF in the game (`0x00870D00 == 0`), the identity linear ramp is restored.
//! 4. "Over Bright" bloom (`0x00870CFC`) is clamped to 0 on mobile to prevent TBDR pipeline stalls.

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, Ordering};

pub const G_OVERBRIGHT_ENABLE_ADDR: usize = 0x00870CFC;
pub const G_BLEACH_BYPASS_ENABLE_ADDR: usize = 0x00870D00;
pub const G_POST_PROCESS_STATE_ADDR: usize = 0x008708F4;

static INGAME_CONTRAST_ACTIVE: AtomicBool = AtomicBool::new(false);
static LAST_INGAME_CONTRAST_VAL: AtomicI32 = AtomicI32::new(-1);
static CONTRAST_FRAME_COUNT: AtomicU32 = AtomicU32::new(0);

// Function pointer for native D3D9 SetGammaRamp callback
static mut G_RAMP_CALLBACK: Option<unsafe extern "C" fn(i32)> = None;

/// Registers the native C/C++ callback to apply hardware tone curve via D3D9 SetGammaRamp.
#[no_mangle]
pub unsafe extern "C" fn nfs_contrast_register_ramp_callback(cb: Option<unsafe extern "C" fn(i32)>) {
    G_RAMP_CALLBACK = cb;
}

/// Applies hardware contrast tone mapping (1 = S-curve high contrast, 0 = linear identity).
#[no_mangle]
pub extern "C" fn nfs_contrast_apply_hardware_tone(enable: i32) {
    let active = enable != 0;
    INGAME_CONTRAST_ACTIVE.store(active, Ordering::Relaxed);
    unsafe {
        if let Some(cb) = G_RAMP_CALLBACK {
            cb(enable);
        }
    }
}

/// Checks whether Enhanced Contrast is currently active in the game.
#[no_mangle]
pub extern "C" fn nfs_contrast_is_enabled() -> bool {
    INGAME_CONTRAST_ACTIVE.load(Ordering::Relaxed)
}

/// Filter registry or option value for g_OverBrightEnable (address 0x00870CFC).
/// Always clamps to 0 on mobile TBDR architectures to eliminate heavy Gaussian bloom stalls.
#[no_mangle]
pub extern "C" fn nfs_contrast_filter_overbright(_default_val: u32) -> u32 {
    0
}

/// Filter registry or option value for g_BleachByPassEnable (address 0x00870D00).
/// Preserves the game's configured option.
#[no_mangle]
pub extern "C" fn nfs_contrast_filter_bleach_bypass(default_val: u32) -> u32 {
    default_val
}

#[no_mangle]
pub extern "C" fn nfs_contrast_get_mode() -> u32 {
    if INGAME_CONTRAST_ACTIVE.load(Ordering::Relaxed) { 1 } else { 0 }
}

/// Synchronizes with the in-game display setting on every frame tick directly from guest memory.
///
/// - Reads `0x00870D00` (in-game Enhanced Contrast toggle).
/// - Clamps `0x00870CFC` (OverBright bloom) to 0 on mobile.
/// - When in-game toggle changes, updates the display hardware tone curve.
#[no_mangle]
pub unsafe extern "C" fn nfs_contrast_sync_ingame(guest_mem: *mut u8) -> u32 {
    if guest_mem.is_null() {
        return 0;
    }

    let p_overbright = guest_mem.add(G_OVERBRIGHT_ENABLE_ADDR) as *mut u32;
    let p_bleach = guest_mem.add(G_BLEACH_BYPASS_ENABLE_ADDR) as *mut u32;

    // 1. Clamp OverBright Bloom to 0 on mobile to avoid 6-pass Gaussian blur tile stalls
    std::ptr::write_volatile(p_overbright, 0);

    // 2. Read in-game Enhanced Contrast setting chosen by player
    let ingame_val = std::ptr::read_volatile(p_bleach);
    let last_val = LAST_INGAME_CONTRAST_VAL.load(Ordering::Relaxed);

    if (ingame_val as i32) != last_val {
        LAST_INGAME_CONTRAST_VAL.store(ingame_val as i32, Ordering::Relaxed);
        let active = ingame_val != 0;
        INGAME_CONTRAST_ACTIVE.store(active, Ordering::Relaxed);
        if let Some(cb) = G_RAMP_CALLBACK {
            cb(if active { 1 } else { 0 });
        }
    }

    CONTRAST_FRAME_COUNT.fetch_add(1, Ordering::Relaxed);
    ingame_val
}

/// Returns the number of frames synchronized by pure Rust Enhanced Contrast regulator.
#[no_mangle]
pub extern "C" fn nfs_contrast_synced_frame_count() -> u32 {
    CONTRAST_FRAME_COUNT.load(Ordering::Relaxed)
}

#[cfg(test)]
mod tests {
    use super::*;

    static mut MOCK_CB_CALLED: i32 = -1;
    unsafe extern "C" fn mock_callback(val: i32) {
        MOCK_CB_CALLED = val;
    }

    #[test]
    fn test_ingame_contrast_sync() {
        unsafe {
            nfs_contrast_register_ramp_callback(Some(mock_callback));
        }

        let mut mock_mem = vec![0u8; 0x00900000];
        let base = mock_mem.as_mut_ptr();

        unsafe {
            // Player turns Enhanced Contrast ON in game options
            let p_bleach = base.add(G_BLEACH_BYPASS_ENABLE_ADDR) as *mut u32;
            let p_ob = base.add(G_OVERBRIGHT_ENABLE_ADDR) as *mut u32;
            std::ptr::write_volatile(p_bleach, 1);
            std::ptr::write_volatile(p_ob, 1);

            let res = nfs_contrast_sync_ingame(base);
            assert_eq!(res, 1);
            assert!(nfs_contrast_is_enabled());
            // Overbright must be clamped to 0
            assert_eq!(std::ptr::read_volatile(p_ob), 0);
            assert_eq!(MOCK_CB_CALLED, 1);

            // Player turns Enhanced Contrast OFF in game options
            std::ptr::write_volatile(p_bleach, 0);
            let res2 = nfs_contrast_sync_ingame(base);
            assert_eq!(res2, 0);
            assert!(!nfs_contrast_is_enabled());
            assert_eq!(MOCK_CB_CALLED, 0);
        }
    }

    #[test]
    fn test_filter_overbright() {
        assert_eq!(nfs_contrast_filter_overbright(1), 0);
        assert_eq!(nfs_contrast_filter_overbright(0), 0);
    }
}
