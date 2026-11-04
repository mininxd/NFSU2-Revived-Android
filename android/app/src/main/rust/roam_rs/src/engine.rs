//! Pure Rust engine pacing, high-FPS timing patches, and guest memory control for NFSU2 Android.
//!
//! Manages guest memory addresses for simulation time steps, framerate pacing,
//! high-refresh rate unlocks, projector headlight clamping, and spectator crowd suppression.

use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use crate::interp::pacing_filter_dt;

pub const G_TIMESTEP_ADDR: usize = 0x007875BC;
pub const G_DELTA_ACCUM_ADDR: usize = 0x00865558;
pub const G_FRAME_LIMITER_ADDR: usize = 0x007FB710;
pub const G_NTSC_MODE_ADDR: usize = 0x007FB700;
pub const G_MAX_FRAME_DELTA_ADDR: usize = 0x007FB714;
pub const G_HIGH_FPS_PATCH_ADDR: usize = 0x0057A084;
pub const G_HEADLIGHT_ENABLE_ADDR: usize = 0x00870CC4;
pub const G_CROWD_ENABLE_ADDR: usize = 0x00870CD0;
pub const G_SIM_WDT_ADDR: usize = 0x00865154;
pub const G_SIM_REALDT_ADDR: usize = 0x00865198;
pub const G_SIM_STEPS_ADDR: usize = 0x00865140;

static HIGH_FPS_PATCHED: AtomicBool = AtomicBool::new(false);
static PACED_FRAMES: AtomicU32 = AtomicU32::new(0);

#[inline]
unsafe fn read_f32(mem: *const u8, addr: usize) -> f32 {
    std::ptr::read_volatile(mem.add(addr) as *const f32)
}

#[inline]
unsafe fn write_f32(mem: *mut u8, addr: usize, val: f32) {
    std::ptr::write_volatile(mem.add(addr) as *mut f32, val);
}

#[inline]
unsafe fn read_u32(mem: *const u8, addr: usize) -> u32 {
    std::ptr::read_volatile(mem.add(addr) as *const u32)
}

#[inline]
unsafe fn write_u32(mem: *mut u8, addr: usize, val: u32) {
    std::ptr::write_volatile(mem.add(addr) as *mut u32, val);
}

#[inline]
unsafe fn read_u8(mem: *const u8, addr: usize) -> u8 {
    std::ptr::read_volatile(mem.add(addr))
}

#[inline]
unsafe fn write_u8(mem: *mut u8, addr: usize, val: u8) {
    std::ptr::write_volatile(mem.add(addr), val);
}

/// Apply all engine timing, pacing, and performance patches to guest memory.
///
/// `mem_base`: Pointer to guest 32-bit address space base.
/// `real_elapsed`: Real wall-clock elapsed time (in seconds) since last frame.
#[no_mangle]
pub unsafe extern "C" fn nfs_rust_apply_engine_pacing(mem_base: *mut u8, real_elapsed: f32) -> bool {
    if mem_base.is_null() {
        return false;
    }

    // 1. Simulation time step base (0x007875BC) locked to 60 Hz (0.016666668s)
    // Ensures internal simulation and particle systems advance at correct rate
    write_f32(mem_base, G_TIMESTEP_ADDR, 0.016666668);

    // 2. Disarm busy-spin limiters
    write_f32(mem_base, G_DELTA_ACCUM_ADDR, 0.0);
    write_f32(mem_base, G_FRAME_LIMITER_ADDR, 0.0);

    // 3. Force NTSC 60 Hz mode (0x7FB700 = 1). Prevents PAL 50 Hz lock (20 ms clamp)
    write_u32(mem_base, G_NTSC_MODE_ADDR, 1);

    // 4. Expand maximum allowable frame delta multiplier (0x7FB714) to 6.0f (allows up to 100 ms)
    write_f32(mem_base, G_MAX_FRAME_DELTA_ADDR, 6.0);

    // 5. High refresh rate patch (0x0057A084: 0x75 'jne' -> 0xEB 'jmp')
    // Prevents spurious 'mov eax, 1' when frame time is under 1/60s on 90/120 Hz screens
    if !HIGH_FPS_PATCHED.load(Ordering::Relaxed) {
        if read_u8(mem_base, G_HIGH_FPS_PATCH_ADDR) == 0x75 {
            write_u8(mem_base, G_HIGH_FPS_PATCH_ADDR, 0xEB);
        }
        HIGH_FPS_PATCHED.store(true, Ordering::Relaxed);
    }

    // 6. Clamp g_CarHeadlightEnable (0x00870CC4) to 1 (Player only), eliminating NPC projector shadows
    let hl = read_u32(mem_base, G_HEADLIGHT_ENABLE_ADDR);
    if hl > 1 {
        write_u32(mem_base, G_HEADLIGHT_ENABLE_ADDR, 1);
    }

    // 7. Trackside Crowd Suppression (0x00870CD0 = g_CrowdEnable)
    crate::crowd::nfs_rust_apply_crowd_suppression(mem_base);

    // 8. Anti-slowmotion frame pacing
    if real_elapsed > 0.010 && real_elapsed < 0.200 {
        let current_wdt = read_f32(mem_base, G_SIM_WDT_ADDR);
        let current_realdt = read_f32(mem_base, G_SIM_REALDT_ADDR);
        let effective_dt = if current_wdt > 0.0 { current_wdt } else { current_realdt };

        if effective_dt > 0.0 && effective_dt < real_elapsed * 0.90 {
            let corrected = pacing_filter_dt(real_elapsed, effective_dt);
            write_f32(mem_base, G_SIM_WDT_ADDR, corrected);
            write_f32(mem_base, G_SIM_REALDT_ADDR, corrected);
            write_u32(mem_base, G_SIM_STEPS_ADDR, (corrected * 1800.0) as u32);
            PACED_FRAMES.fetch_add(1, Ordering::Relaxed);
        }
    }

    true
}

/// Total count of frames adjusted by pure Rust frame pacing.
#[no_mangle]
pub extern "C" fn nfs_rust_engine_paced_frames() -> u32 {
    PACED_FRAMES.load(Ordering::Relaxed)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_memory_layout_offsets() {
        assert_eq!(G_TIMESTEP_ADDR, 0x007875BC);
        assert_eq!(G_CROWD_ENABLE_ADDR, 0x00870CD0);
        assert_eq!(G_HIGH_FPS_PATCH_ADDR, 0x0057A084);
    }
}
