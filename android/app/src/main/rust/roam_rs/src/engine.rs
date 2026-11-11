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
pub const G_OVERBRIGHT_ENABLE_ADDR: usize = 0x00870CFC;
pub const G_BLEACH_BYPASS_ENABLE_ADDR: usize = 0x00870D00;
pub const G_SIM_WDT_ADDR: usize = 0x00865154;
pub const G_SIM_REALDT_ADDR: usize = 0x00865198;
pub const G_SIM_STEPS_ADDR: usize = 0x00865140;

static HIGH_FPS_PATCHED: AtomicBool = AtomicBool::new(false);
static PACED_FRAMES: AtomicU32 = AtomicU32::new(0);
static CONTRAST_INITIALIZED: AtomicBool = AtomicBool::new(false);
static CONTRAST_MODE: AtomicU32 = AtomicU32::new(1); // 0 = Off, 1 = Optimized, 2 = Original PC

/// Pure Rust parser for Android SharedPreferences XML without any Java runtime.
pub fn parse_shared_prefs_xml(xml: &str, key: &str) -> Option<String> {
    let search_tag = format!("<string name=\"{}\">", key);
    if let Some(start_idx) = xml.find(&search_tag) {
        let val_start = start_idx + search_tag.len();
        if let Some(end_idx) = xml[val_start..].find("</string>") {
            return Some(xml[val_start..val_start + end_idx].trim().to_string());
        }
    }
    None
}

/// Detects enhanced contrast mode from system environment, SharedPreferences XML,
/// or game storage INI files purely in Rust.
pub fn nfs_rust_detect_contrast_mode() -> u32 {
    // 1. Direct environment variable check
    if let Ok(env_val) = std::env::var("NFSU2_CONTRAST") {
        let trimmed = env_val.trim();
        if trimmed.eq_ignore_ascii_case("off") || trimmed == "0" {
            return 0;
        } else if trimmed.eq_ignore_ascii_case("on") || trimmed == "2" || trimmed.eq_ignore_ascii_case("original") {
            return 2;
        } else if !trimmed.is_empty() {
            return 1;
        }
    }

    // 2. Direct read from Android SharedPreferences XML on disk
    let pref_paths = [
        "/data/data/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml",
        "/data/user/0/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml",
    ];

    for path in &pref_paths {
        if let Ok(content) = std::fs::read_to_string(path) {
            if let Some(val) = parse_shared_prefs_xml(&content, "enhanced_contrast") {
                if val.eq_ignore_ascii_case("off") || val == "0" {
                    return 0;
                } else if val.eq_ignore_ascii_case("on") || val == "2" || val.eq_ignore_ascii_case("original") {
                    return 2;
                } else {
                    return 1;
                }
            }
        }
    }

    // 3. Fallback: check WidescreenFix.ini or game scripts
    let ini_paths = [
        "/sdcard/nfsu2/scripts/NFSUnderground2.WidescreenFix.ini",
        "/storage/emulated/0/nfsu2/scripts/NFSUnderground2.WidescreenFix.ini",
        "scripts/NFSUnderground2.WidescreenFix.ini",
    ];
    for path in &ini_paths {
        if let Ok(content) = std::fs::read_to_string(path) {
            for line in content.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with("ConsoleGamma") {
                    if let Some(eq_idx) = trimmed.find('=') {
                        let after_eq = trimmed[eq_idx + 1..].trim();
                        let token = after_eq.split(|c: char| c.is_whitespace() || c == ';').next().unwrap_or("");
                        if token == "0" {
                            return 0;
                        }
                    }
                }
            }
        }
    }

    // Default: Optimized (1) - Solid 60 FPS on Adreno mobile GPUs
    1
}

/// Automatically configures Enhanced Contrast purely in Rust.
#[no_mangle]
pub extern "C" fn nfs_rust_auto_configure_contrast() -> u32 {
    let mode = nfs_rust_detect_contrast_mode();
    CONTRAST_MODE.store(mode, Ordering::Relaxed);
    CONTRAST_INITIALIZED.store(true, Ordering::Relaxed);

    // Export to process environment for any native C/C++ or DXVK components
    let env_str = match mode {
        0 => b"off\0".as_ptr(),
        2 => b"on\0".as_ptr(),
        _ => b"optimized\0".as_ptr(),
    };
    unsafe {
        libc::setenv(
            b"NFSU2_CONTRAST\0".as_ptr() as *const libc::c_char,
            env_str as *const libc::c_char,
            1,
        );
    }

    mode
}

/// Configures runtime Enhanced Contrast regulation mode.
#[no_mangle]
pub extern "C" fn nfs_rust_set_contrast_mode(mode: u32) {
    CONTRAST_MODE.store(mode, Ordering::Relaxed);
    CONTRAST_INITIALIZED.store(true, Ordering::Relaxed);
}

/// Returns active Enhanced Contrast regulation mode.
#[no_mangle]
pub extern "C" fn nfs_rust_get_contrast_mode() -> u32 {
    if !CONTRAST_INITIALIZED.load(Ordering::Relaxed) {
        nfs_rust_auto_configure_contrast();
    }
    CONTRAST_MODE.load(Ordering::Relaxed)
}

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

    // 8. Enhanced Contrast Regulation (Issue #70: Snapdragon 8 Elite / Adreno 840 / 740)
    // 8. Enhanced Contrast Regulation: clamp OverBright bloom to 0 to prevent TBDR tile stalls,
    // while respecting the player's in-game Enhanced Contrast choice (G_BLEACH_BYPASS_ENABLE_ADDR)
    write_u32(mem_base, G_OVERBRIGHT_ENABLE_ADDR, 0);

    // 9. Anti-slowmotion frame pacing
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
        assert_eq!(G_OVERBRIGHT_ENABLE_ADDR, 0x00870CFC);
        assert_eq!(G_BLEACH_BYPASS_ENABLE_ADDR, 0x00870D00);
        assert_eq!(G_HIGH_FPS_PATCH_ADDR, 0x0057A084);
    }

    #[test]
    fn test_contrast_mode_regulation() {
        nfs_rust_set_contrast_mode(0);
        assert_eq!(nfs_rust_get_contrast_mode(), 0);
        nfs_rust_set_contrast_mode(1);
        assert_eq!(nfs_rust_get_contrast_mode(), 1);
        nfs_rust_set_contrast_mode(2);
        assert_eq!(nfs_rust_get_contrast_mode(), 2);
    }

    #[test]
    fn test_parse_shared_prefs_xml() {
        let sample = "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n\
                      <map>\n\
                          <string name=\"enhanced_contrast\">optimized</string>\n\
                          <string name=\"fps\">60</string>\n\
                      </map>";
        assert_eq!(parse_shared_prefs_xml(sample, "enhanced_contrast"), Some("optimized".to_string()));
        assert_eq!(parse_shared_prefs_xml(sample, "fps"), Some("60".to_string()));
        assert_eq!(parse_shared_prefs_xml(sample, "non_existent"), None);
    }
}
