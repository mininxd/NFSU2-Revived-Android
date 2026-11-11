//! Pure Rust Enhanced Contrast and Tone Mapping Regulator for NFSU2 Android.
//!
//! Controls Enhanced Contrast (Bleach Bypass vs OverBright Bloom) post-processing passes.
//! On mobile Adreno GPUs (e.g. Snapdragon 8 Elite / Adreno 840, Snapdragon 8 Gen 2 / Adreno 740),
//! running both multi-pass OverBright bloom and Bleach Bypass generates 6+ render target resolves
//! and heavy pipeline barriers per frame, causing FPS to drop from 60 to 35-45.
//!
//! Modes:
//! - 0 = Off (Fastest): Both OverBright and Bleach Bypass disabled.
//! - 1 = Optimized (Smooth 60 FPS): Bleach Bypass enabled for vivid high-contrast aesthetic,
//!       OverBright bloom disabled to eliminate heavy TBDR tile resolves and frame hitching.
//! - 2 = Original (High GPU Load): Both OverBright and Bleach Bypass enabled (PC original behavior).

use std::fs;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

pub const CONTRAST_MODE_OFF: u32 = 0;
pub const CONTRAST_MODE_OPTIMIZED: u32 = 1;
pub const CONTRAST_MODE_ORIGINAL: u32 = 2;

static CONTRAST_INITIALIZED: AtomicBool = AtomicBool::new(false);
static CONTRAST_MODE: AtomicU32 = AtomicU32::new(CONTRAST_MODE_OPTIMIZED);

/// Pure Rust parser for Android SharedPreferences XML without any Java runtime.
fn parse_shared_prefs_xml(xml: &str, key: &str) -> Option<String> {
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
pub fn detect_contrast_mode() -> u32 {
    // 1. Direct environment variable check
    if let Ok(env_val) = std::env::var("NFSU2_CONTRAST") {
        let trimmed = env_val.trim();
        if trimmed.eq_ignore_ascii_case("off") || trimmed == "0" {
            return CONTRAST_MODE_OFF;
        } else if trimmed.eq_ignore_ascii_case("on") || trimmed == "2" || trimmed.eq_ignore_ascii_case("original") {
            return CONTRAST_MODE_ORIGINAL;
        } else if !trimmed.is_empty() {
            return CONTRAST_MODE_OPTIMIZED;
        }
    }

    // 2. Direct read from Android SharedPreferences XML on disk
    let pref_paths = [
        "/data/data/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml",
        "/data/user/0/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml",
    ];

    for path in &pref_paths {
        if let Ok(content) = fs::read_to_string(path) {
            if let Some(val) = parse_shared_prefs_xml(&content, "enhanced_contrast") {
                if val.eq_ignore_ascii_case("off") || val == "0" {
                    return CONTRAST_MODE_OFF;
                } else if val.eq_ignore_ascii_case("on") || val == "2" || val.eq_ignore_ascii_case("original") {
                    return CONTRAST_MODE_ORIGINAL;
                } else {
                    return CONTRAST_MODE_OPTIMIZED;
                }
            }
        }
    }

    // Default: Optimized (1) - Solid 60 FPS on Adreno mobile GPUs
    CONTRAST_MODE_OPTIMIZED
}

/// Automatically configures Enhanced Contrast purely in Rust.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_auto_configure_contrast() -> u32 {
    let mode = detect_contrast_mode();
    CONTRAST_MODE.store(mode, Ordering::Relaxed);
    CONTRAST_INITIALIZED.store(true, Ordering::Relaxed);

    // Export to process environment for any native C/C++ or DXVK components
    let env_str = match mode {
        CONTRAST_MODE_OFF => b"off\0".as_ptr(),
        CONTRAST_MODE_ORIGINAL => b"on\0".as_ptr(),
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

/// Pure Rust C-ABI setter: sets contrast regulation mode.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_set_contrast_mode(mode: u32) {
    CONTRAST_MODE.store(mode, Ordering::Relaxed);
    CONTRAST_INITIALIZED.store(true, Ordering::Relaxed);
}

/// Pure Rust C-ABI getter: gets active contrast regulation mode.
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_get_contrast_mode() -> u32 {
    if !CONTRAST_INITIALIZED.load(Ordering::Relaxed) {
        nfs_gfx_rust_auto_configure_contrast();
    }
    CONTRAST_MODE.load(Ordering::Relaxed)
}

/// Filter registry or option value for g_OverBrightEnable (address 0x00870CFC).
/// Returns 1 only in Original mode (2); returns 0 in Optimized (1) and Off (0).
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_filter_overbright(default_val: u32) -> u32 {
    let mode = nfs_gfx_rust_get_contrast_mode();
    match mode {
        CONTRAST_MODE_OFF | CONTRAST_MODE_OPTIMIZED => 0,
        CONTRAST_MODE_ORIGINAL => default_val.max(1),
        _ => 0,
    }
}

/// Filter registry or option value for g_BleachByPassEnable (address 0x00870D00).
/// Returns 1 in Optimized (1) and Original (2); returns 0 in Off (0).
#[no_mangle]
pub extern "C" fn nfs_gfx_rust_filter_bleach_bypass(default_val: u32) -> u32 {
    let mode = nfs_gfx_rust_get_contrast_mode();
    match mode {
        CONTRAST_MODE_OFF => 0,
        CONTRAST_MODE_OPTIMIZED | CONTRAST_MODE_ORIGINAL => default_val.max(1),
        _ => default_val,
    }
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
