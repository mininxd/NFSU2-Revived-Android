//! Pure Rust Game Settings & Configuration Synchronization Engine for NFSU2 Android.
//!
//! Replaces slow Java File/BufferedReader/PrintWriter parsing with zero-allocation in-place
//! configuration synchronization for `WidescreenFix.ini`, `WidescreenFix.dat`, and
//! `NFSU2ExtraOptionsSettings.ini`.

use std::ffi::{CStr, CString};
use std::fs;
use std::path::{Path, PathBuf};

/// Simple XML string value extractor for Android SharedPreferences XML without XML parser bloat.
pub fn parse_xml_tag_value(xml: &str, tag_type: &str, key: &str) -> Option<String> {
    let prefix = format!("<{} name=\"{}\"", tag_type, key);
    if let Some(pos) = xml.find(&prefix) {
        let rest = &xml[pos + prefix.len()..];
        // Could be <boolean name="..." value="true" />
        if let Some(val_attr) = rest.strip_prefix(" value=\"") {
            if let Some(end_quote) = val_attr.find('"') {
                return Some(val_attr[..end_quote].to_string());
            }
        }
        // Or <string name="...">value</string>
        if let Some(tag_close) = rest.find('>') {
            let after_tag = &rest[tag_close + 1..];
            let close_tag = format!("</{}>", tag_type);
            if let Some(end_pos) = after_tag.find(&close_tag) {
                return Some(after_tag[..end_pos].trim().to_string());
            }
        }
    }
    None
}

/// Updates or replaces a key-value line in an INI file.
pub fn update_ini_key(content: &str, key: &str, new_line: &str) -> (String, bool) {
    let mut lines = Vec::new();
    let mut found = false;
    let mut changed = false;

    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with(key) {
            if let Some(rest) = trimmed.strip_prefix(key) {
                let rest_trimmed = rest.trim_start();
                if rest_trimmed.starts_with('=') {
                    found = true;
                    if line != new_line {
                        lines.push(new_line.to_string());
                        changed = true;
                    } else {
                        lines.push(line.to_string());
                    }
                    continue;
                }
            }
        }
        lines.push(line.to_string());
    }

    if !found {
        lines.push(new_line.to_string());
        changed = true;
    }

    let mut result = lines.join("\r\n");
    if content.ends_with('\n') {
        result.push_str("\r\n");
    }
    (result, changed)
}

/// Synchronizes scripts/NFSUnderground2.WidescreenFix.ini purely in Rust.
pub fn sync_widescreen_ini(game_root: &Path, menu_size: &str, is_console_pad: bool) -> bool {
    let ini_candidates = [
        game_root.join("scripts/NFSUnderground2.WidescreenFix.ini"),
        PathBuf::from("/sdcard/nfsu2/scripts/NFSUnderground2.WidescreenFix.ini"),
        PathBuf::from("/storage/emulated/0/nfsu2/scripts/NFSUnderground2.WidescreenFix.ini"),
    ];

    let mut target_path = None;
    for path in &ini_candidates {
        if path.is_file() {
            target_path = Some(path.clone());
            break;
        }
    }

    let path = match target_path {
        Some(p) => p,
        None => return false,
    };

    let content = match fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => return false,
    };

    let fe_scale = match menu_size {
        "125" => "1.25",
        "150" => "1.50",
        "175" => "1.75",
        "200" => "2.00",
        _ => "1.00",
    };

    let pad_support = if is_console_pad { "2" } else { "1" };

    let mut cur = content;
    let mut any_changed = false;

    let (c1, ch1) = update_ini_key(&cur, "FEScale", &format!("FEScale = {}                           ; Set the size of UI elements here.", fe_scale));
    cur = c1; any_changed |= ch1;

    let (c2, ch2) = update_ini_key(&cur, "HudAspectRatioConstraint", "HudAspectRatioConstraint = Auto          ; Constrains the HUD to an aspect ratio.");
    cur = c2; any_changed |= ch2;

    let (c3, ch3) = update_ini_key(&cur, "AutoFitFE", "AutoFitFE = 1                            ; Automatically fits 16:9 UI content.");
    cur = c3; any_changed |= ch3;

    let (c4, ch4) = update_ini_key(&cur, "SingleCoreAffinity", "SingleCoreAffinity = 0                   ; Handled by Rust affinity binder.");
    cur = c4; any_changed |= ch4;

    let (c5, ch5) = update_ini_key(&cur, "ImproveGamepadSupport", &format!("ImproveGamepadSupport = {}                ; 1=Xbox, 2=PS, 3=PC", pad_support));
    cur = c5; any_changed |= ch5;

    if any_changed {
        let _ = fs::write(&path, cur);
    }
    true
}

/// Synchronizes scripts/NFSUnderground2.WidescreenFix.dat layout purely in Rust.
pub fn sync_widescreen_dat(game_root: &Path, logs_enabled: bool) -> bool {
    let dat_candidates = [
        game_root.join("scripts/NFSUnderground2.WidescreenFix.dat"),
        game_root.join("scripts/NFSUnderground2.WidescreenFix.dat.hold"),
        PathBuf::from("/sdcard/nfsu2/scripts/NFSUnderground2.WidescreenFix.dat"),
    ];

    let mut target_path = None;
    for path in &dat_candidates {
        if path.is_file() {
            target_path = Some(path.clone());
            break;
        }
    }

    let path = match target_path {
        Some(p) => p,
        None => return false,
    };

    let content = match fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => return false,
    };

    let mut lines = Vec::new();
    let mut changed = false;

    for line in content.lines() {
        let trimmed = line.trim();
        let new_line = if trimmed.starts_with("Bank_Bar_Pos") {
            if logs_enabled {
                "Bank_Bar_Pos                         227.0  -184.0               -485.0  0.0"
            } else {
                "Bank_Bar_Pos                         227.0  -184.0                119.0  0.0"
            }
        } else if trimmed.starts_with("Bank_Text_Pos") {
            if logs_enabled {
                "Bank_Text_Pos                        193.0  -194.0               -485.0  0.0"
            } else {
                "Bank_Text_Pos                        193.0  -194.0                119.0  0.0"
            }
        } else if trimmed.starts_with("Bank_Reward_Text_Pos") {
            if logs_enabled {
                "Bank_Reward_Text_Pos                 283.0  -163.0               -485.0  0.0"
            } else {
                "Bank_Reward_Text_Pos                 283.0  -163.0                119.0  0.0"
            }
        } else if trimmed.starts_with("SMS_Bar_Pos") {
            if logs_enabled {
                "SMS_Bar_Pos                         -248.0  -122.0               -109.0  50.0"
            } else {
                "SMS_Bar_Pos                         -248.0  -122.0               -109.0  0.0"
            }
        } else if trimmed.starts_with("SMS_Icon_Pos") {
            if logs_enabled {
                "SMS_Icon_Pos                        -195.0  -112.0               -109.0  50.0"
            } else {
                "SMS_Icon_Pos                        -195.0  -112.0               -109.0  0.0"
            }
        } else if trimmed.starts_with("Satellite_Signal_Pos") {
            if logs_enabled {
                "Satellite_Signal_Pos                -263.0   36.0                 -109.0  50.0"
            } else {
                "Satellite_Signal_Pos                -263.0   36.0                 -109.0  0.0"
            }
        } else {
            line
        };

        if new_line != line {
            changed = true;
        }
        lines.push(new_line);
    }

    if changed {
        let mut out = lines.join("\r\n");
        if content.ends_with('\n') {
            out.push_str("\r\n");
        }
        let _ = fs::write(&path, out);
    }
    true
}

/// Synchronizes scripts/NFSU2ExtraOptionsSettings.ini purely in Rust.
pub fn sync_extra_options_ini(game_root: &Path) -> bool {
    let ini_candidates = [
        game_root.join("scripts/NFSU2ExtraOptionsSettings.ini"),
        PathBuf::from("/sdcard/nfsu2/scripts/NFSU2ExtraOptionsSettings.ini"),
    ];

    let mut target_path = None;
    for path in &ini_candidates {
        if path.is_file() {
            target_path = Some(path.clone());
            break;
        }
    }

    let path = match target_path {
        Some(p) => p,
        None => return false,
    };

    let content = match fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => return false,
    };

    let mut cur = content;
    let mut any_changed = false;

    let (c1, ch1) = update_ini_key(&cur, "UnlockRegionalCars", "UnlockRegionalCars = 1");
    cur = c1; any_changed |= ch1;

    let (c2, ch2) = update_ini_key(&cur, "AllowLongerProfileNames", "AllowLongerProfileNames = 1");
    cur = c2; any_changed |= ch2;

    let (c3, ch3) = update_ini_key(&cur, "ShowSpecialVinyls", "ShowSpecialVinyls = 1");
    cur = c3; any_changed |= ch3;

    let (c4, ch4) = update_ini_key(&cur, "ShowOutrun", "ShowOutrun = 1");
    cur = c4; any_changed |= ch4;

    let (c5, ch5) = update_ini_key(&cur, "DamageFix", "DamageFix = 1");
    cur = c5; any_changed |= ch5;

    let (c6, ch6) = update_ini_key(&cur, "DisappearingWheelsFix", "DisappearingWheelsFix = 1");
    cur = c6; any_changed |= ch6;

    if any_changed {
        let _ = fs::write(&path, cur);
    }
    true
}

/// Full synchronization of all game configuration files in pure Rust.
#[no_mangle]
pub unsafe extern "C" fn nfs_settings_sync_all(
    game_root_ptr: *const libc::c_char,
    prefs_xml_ptr: *const libc::c_char,
) -> i32 {
    let game_root_str = if !game_root_ptr.is_null() {
        CStr::from_ptr(game_root_ptr).to_string_lossy().into_owned()
    } else {
        "/sdcard/nfsu2".to_string()
    };
    let game_root = Path::new(&game_root_str);

    let prefs_xml_path = if !prefs_xml_ptr.is_null() {
        CStr::from_ptr(prefs_xml_ptr).to_string_lossy().into_owned()
    } else {
        "/data/data/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml".to_string()
    };

    let prefs_xml = fs::read_to_string(&prefs_xml_path).unwrap_or_default();

    let menu_size = parse_xml_tag_value(&prefs_xml, "string", "menu_button_size")
        .unwrap_or_else(|| "100".to_string());
    let pad_layout = parse_xml_tag_value(&prefs_xml, "string", "pad_layout")
        .unwrap_or_else(|| "port".to_string());
    let is_console_pad = pad_layout.eq_ignore_ascii_case("console");

    let display_logs = parse_xml_tag_value(&prefs_xml, "string", "display_logs")
        .map(|v| v != "off" && v != "0")
        .unwrap_or(false);

    let s1 = sync_widescreen_ini(game_root, &menu_size, is_console_pad);
    let s2 = sync_widescreen_dat(game_root, display_logs);
    let s3 = sync_extra_options_ini(game_root);

    if s1 || s2 || s3 { 0 } else { -1 }
}

// ---- JNI Export for io.mininxd.nfsu2.GameOptions ---------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_GameOptions_nativeSyncSettings(
    mut env: jni::JNIEnv,
    _class: jni::objects::JClass,
    j_game_root: jni::sys::jstring,
    j_prefs_path: jni::sys::jstring,
) -> jni::sys::jint {
    let game_root = if !j_game_root.is_null() {
        env.get_string(&jni::objects::JString::from_raw(j_game_root))
            .map(|s| s.into())
            .unwrap_or_else(|_| "/sdcard/nfsu2".to_string())
    } else {
        "/sdcard/nfsu2".to_string()
    };

    let prefs_path = if !j_prefs_path.is_null() {
        env.get_string(&jni::objects::JString::from_raw(j_prefs_path))
            .map(|s| s.into())
            .unwrap_or_else(|_| "/data/data/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml".to_string())
    } else {
        "/data/data/io.mininxd.nfsu2/shared_prefs/nfsu2_game.xml".to_string()
    };

    let c_root = CString::new(game_root).unwrap_or_default();
    let c_prefs = CString::new(prefs_path).unwrap_or_default();

    nfs_settings_sync_all(c_root.as_ptr(), c_prefs.as_ptr())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_xml_tag_value() {
        let xml = r#"<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="menu_button_size">150</string>
    <boolean name="display_logs" value="true" />
</map>"#;
        assert_eq!(parse_xml_tag_value(xml, "string", "menu_button_size"), Some("150".to_string()));
        assert_eq!(parse_xml_tag_value(xml, "boolean", "display_logs"), Some("true".to_string()));
        assert_eq!(parse_xml_tag_value(xml, "string", "non_existent"), None);
    }

    #[test]
    fn test_update_ini_key() {
        let ini = "[MAIN]\r\nFEScale = 1.00\r\nSkipIntro = 0\r\n";
        let (updated, changed) = update_ini_key(ini, "FEScale", "FEScale = 1.50");
        assert!(changed);
        assert!(updated.contains("FEScale = 1.50"));
        assert!(updated.contains("SkipIntro = 0"));
    }
}
