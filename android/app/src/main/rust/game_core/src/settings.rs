//! Game options and INI synchronization engine in pure Rust.
//! Replaces Java string manipulation and disk file re-writing with zero-overhead native methods.

use std::fs::{self, File};
use std::io::{BufRead, BufReader, Write};
use std::path::Path;

use jni::objects::{JClass, JString};
use jni::sys::{jboolean, jint};
use jni::JNIEnv;

/// Synchronizes scripts/NFSUnderground2.WidescreenFix.ini with launcher options.
pub fn sync_widescreen_ini(
    ini_path: &str,
    res_x: i32,
    res_y: i32,
    hud_mode: i32,
    aspect: i32,
    skip_intro: i32,
    cutscene_borders: i32,
    pad_support: i32,
    fe_scale: &str,
) -> bool {
    let path = Path::new(ini_path);
    if !path.is_file() {
        return false;
    }

    let file = match File::open(path) {
        Ok(f) => f,
        Err(_) => return false,
    };

    let reader = BufReader::new(file);
    let mut lines = Vec::with_capacity(128);
    let mut changed = false;

    for line_res in reader.lines() {
        let line = match line_res {
            Ok(l) => l,
            Err(_) => return false,
        };

        let trimmed = line.trim();
        let mut new_line = line.clone();

        if trimmed.starts_with("ResX") && res_x > 0 {
            let target = format!("ResX = {}", res_x);
            if !line.starts_with(&target) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("ResY") && res_y > 0 {
            let target = format!("ResY = {}", res_y);
            if !line.starts_with(&target) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("FEScale") && !fe_scale.is_empty() {
            let target = format!("FEScale = {}                           ; Set the size of UI elements here. (Default = 1.00, Xbox = 0.92)", fe_scale);
            if !line.starts_with(&format!("FEScale = {}", fe_scale)) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("HudAspectRatioConstraint") {
            let target = "HudAspectRatioConstraint = Auto          ; Constrains the HUD to an aspect ratio. Use Auto, 16:9, 21:9, or a custom ratio.".to_string();
            if !line.starts_with("HudAspectRatioConstraint = Auto") {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("AutoFitFE") {
            let target = "AutoFitFE = 1                            ; Automatically fits 16:9 UI content to smaller aspect ratios.".to_string();
            if !line.starts_with("AutoFitFE = 1") {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("SingleCoreAffinity") {
            let target = "SingleCoreAffinity = 0                   ; Performance core affinity manager handles CPU allocation.".to_string();
            if !line.starts_with("SingleCoreAffinity = 0") {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("HudMode") {
            let target = format!("HudMode = {}                             ; 0 = original 4:3, 1 = modern 16:9, 2 = modern widescreen.", hud_mode);
            if !line.starts_with(&format!("HudMode = {}", hud_mode)) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("AspectRatio") {
            let target = format!("AspectRatio = {}                         ; 0 = auto, 1 = 4:3, 2 = 16:9, etc.", aspect);
            if !line.starts_with(&format!("AspectRatio = {}", aspect)) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("SkipIntro") {
            let target = format!("SkipIntro = {}                            ; Skips FMVs that play when you launch the game.", skip_intro);
            if !line.starts_with(&format!("SkipIntro = {}", skip_intro)) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("DisableCutsceneBorders") {
            let target = format!("DisableCutsceneBorders = {}               ; Removes letterboxing that appears during cutscenes.", cutscene_borders);
            if !line.starts_with(&format!("DisableCutsceneBorders = {}", cutscene_borders)) {
                new_line = target;
                changed = true;
            }
        } else if trimmed.starts_with("ImproveGamepadSupport") {
            let target = format!("ImproveGamepadSupport = {}                ; Adds text to buttons and assigns front-end actions. Requires an XInput gamepad. (1 = Xbox Text | 2 = PlayStation Text | 3 = PC Text | 4 = None)", pad_support);
            if !line.starts_with(&format!("ImproveGamepadSupport = {}", pad_support)) {
                new_line = target;
                changed = true;
            }
        }

        lines.push(new_line);
    }

    if changed {
        if let Ok(mut out_file) = File::create(path) {
            for l in &lines {
                let _ = writeln!(out_file, "{}", l);
            }
            return true;
        }
    }

    true
}

/// Synchronizes scripts/NFSUnderground2.WidescreenFix.dat layout for telemetry logs.
pub fn sync_widescreen_dat(dat_path: &str, logs_enabled: bool) -> bool {
    let path = Path::new(dat_path);
    if !path.is_file() {
        return false;
    }

    let file = match File::open(path) {
        Ok(f) => f,
        Err(_) => return false,
    };

    let reader = BufReader::new(file);
    let mut lines = Vec::with_capacity(128);
    let mut changed = false;

    for line_res in reader.lines() {
        let line = match line_res {
            Ok(l) => l,
            Err(_) => return false,
        };

        let trimmed = line.trim();
        let mut new_line = line.clone();

        if trimmed.starts_with("Bank_Bar_Pos") {
            let target = if logs_enabled {
                "Bank_Bar_Pos                         227.0  -184.0               -485.0  0.0"
            } else {
                "Bank_Bar_Pos                         227.0  -184.0                119.0  0.0"
            };
            if line != target {
                new_line = target.to_string();
                changed = true;
            }
        } else if trimmed.starts_with("Bank_Text_Pos") {
            let target = if logs_enabled {
                "Bank_Text_Pos                        193.0  -194.0               -485.0  0.0"
            } else {
                "Bank_Text_Pos                        193.0  -194.0                119.0  0.0"
            };
            if line != target {
                new_line = target.to_string();
                changed = true;
            }
        } else if trimmed.starts_with("Bank_Reward_Text_Pos") {
            let target = if logs_enabled {
                "Bank_Reward_Text_Pos                 283.0  -163.0               -485.0  0.0"
            } else {
                "Bank_Reward_Text_Pos                 283.0  -163.0                119.0  0.0"
            };
            if line != target {
                new_line = target.to_string();
                changed = true;
            }
        } else if trimmed.starts_with("SMS_Bar_Pos") {
            let target = if logs_enabled {
                "SMS_Bar_Pos                         -248.0  -122.0               -109.0  50.0"
            } else {
                "SMS_Bar_Pos                         -248.0  -122.0               -109.0  0.0"
            };
            if line != target {
                new_line = target.to_string();
                changed = true;
            }
        } else if trimmed.starts_with("SMS_Icon_Pos") {
            let target = if logs_enabled {
                "SMS_Icon_Pos                        -195.0  -112.0               -109.0  50.0"
            } else {
                "SMS_Icon_Pos                        -195.0  -112.0               -109.0  0.0"
            };
            if line != target {
                new_line = target.to_string();
                changed = true;
            }
        } else if trimmed.starts_with("Satellite_Signal_Pos") {
            let target = if logs_enabled {
                "Satellite_Signal_Pos                -263.0   36.0                 -109.0  50.0"
            } else {
                "Satellite_Signal_Pos                -263.0   36.0                 -109.0  0.0"
            };
            if line != target {
                new_line = target.to_string();
                changed = true;
            }
        }

        lines.push(new_line);
    }

    if changed {
        if let Ok(mut out_file) = File::create(path) {
            for l in &lines {
                let _ = writeln!(out_file, "{}", l);
            }
            return true;
        }
    }

    true
}

/// Synchronizes scripts/NFSU2ExtraOptionsSettings.ini settings with launcher options.
pub fn sync_extra_options_ini(
    ini_path: &str,
    regional: bool,
    lap_ctrl: bool,
    _opp_ctrl: bool,
    lap_pct: bool,
    drift: bool,
    fixes: bool,
    menu: bool,
) -> bool {
    let path = Path::new(ini_path);
    if !path.is_file() {
        return false;
    }

    let file = match File::open(path) {
        Ok(f) => f,
        Err(_) => return false,
    };

    let reader = BufReader::new(file);
    let mut out_lines = Vec::with_capacity(256);

    for line_res in reader.lines() {
        let line = match line_res {
            Ok(l) => l,
            Err(_) => return false,
        };

        let trimmed = line.trim();
        if trimmed.starts_with("UnlockRegionalCars") {
            out_lines.push(format!("UnlockRegionalCars = {}", if regional { "1" } else { "0" }));
        } else if trimmed.starts_with("ShowPercentOn1LapRaces") {
            out_lines.push(format!("ShowPercentOn1LapRaces = {}", if lap_pct { "1" } else { "0" }));
        } else if trimmed.starts_with("DisappearingWheelsFix") {
            out_lines.push(format!("DisappearingWheelsFix = {}", if fixes { "1" } else { "0" }));
        } else if trimmed.starts_with("DamageFix") {
            out_lines.push(format!("DamageFix = {}", if fixes { "1" } else { "0" }));
        } else if trimmed.starts_with("MegalowPartsFix") {
            out_lines.push(format!("MegalowPartsFix = {}", if fixes { "1" } else { "0" }));
        } else if trimmed.starts_with("ShowOutrun") {
            out_lines.push(format!("ShowOutrun = {}", if menu { "1" } else { "0" }));
        } else if trimmed.starts_with("FreeRunTrackSelect") {
            out_lines.push(format!("FreeRunTrackSelect = {}", if menu { "1" } else { "0" }));
        } else if trimmed.starts_with("OutrunTrackSelect") {
            out_lines.push(format!("OutrunTrackSelect = {}", if menu { "1" } else { "0" }));
        } else if trimmed.starts_with("ShowWithoutMultiplying") {
            out_lines.push(format!("ShowWithoutMultiplying = {}", if drift { "1" } else { "0" }));
        } else if trimmed.starts_with("PlusSign") {
            out_lines.push(format!("PlusSign = {}", if drift { "1" } else { "0" }));
        } else if trimmed.starts_with("MaximumMultiplier") {
            out_lines.push(format!("MaximumMultiplier = {}", if drift { "9" } else { "5" }));
        } else if trimmed.starts_with("Minimum") && !line.contains("OpponentControllers") {
            out_lines.push(format!("Minimum = {}", if lap_ctrl { "0" } else { "1" }));
        } else if trimmed.starts_with("Maximum") && !line.contains("OpponentControllers") {
            out_lines.push(format!("Maximum = {}", if lap_ctrl { "127" } else { "10" }));
        } else {
            out_lines.push(line);
        }
    }

    if let Ok(mut out_file) = File::create(path) {
        for l in &out_lines {
            let _ = writeln!(out_file, "{}", l);
        }
        return true;
    }

    false
}

/// Detects game region (EU = 1, NA/Default = -1) by searching for GLOBALTEXTURESROW.BIN.
pub fn detect_region(paths: &[&str]) -> i32 {
    for p in paths {
        let candidate = Path::new(p).join("GLOBALTEXTURESROW.BIN");
        if candidate.is_file() {
            return 1;
        }
    }
    -1
}

// ---------------------------------------------------------------------------
// JNI Exports for io.mininxd.nfsu2.GameOptions
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GameOptions_nativeSyncWidescreenIni(
    mut env: JNIEnv,
    _class: JClass,
    path: JString,
    res_x: jint,
    res_y: jint,
    hud_mode: jint,
    aspect: jint,
    skip_intro: jint,
    cutscene_borders: jint,
    pad_support: jint,
    fe_scale: JString,
) -> jboolean {
    if path.is_null() {
        return 0;
    }
    let scale_str = if !fe_scale.is_null() {
        env.get_string(&fe_scale).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default()
    } else {
        String::new()
    };
    if let Ok(p) = env.get_string(&path) {
        let rust_path = p.to_string_lossy();
        if sync_widescreen_ini(&rust_path, res_x, res_y, hud_mode, aspect, skip_intro, cutscene_borders, pad_support, &scale_str) {
            1
        } else {
            0
        }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GameOptions_nativeSyncWidescreenDat(
    mut env: JNIEnv,
    _class: JClass,
    path: JString,
    logs_enabled: jboolean,
) -> jboolean {
    if path.is_null() {
        return 0;
    }
    if let Ok(p) = env.get_string(&path) {
        let rust_path = p.to_string_lossy();
        if sync_widescreen_dat(&rust_path, logs_enabled != 0) {
            1
        } else {
            0
        }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GameOptions_nativeSyncExtraOptionsIni(
    mut env: JNIEnv,
    _class: JClass,
    path: JString,
    regional: jboolean,
    lap_ctrl: jboolean,
    opp_ctrl: jboolean,
    lap_pct: jboolean,
    drift: jboolean,
    fixes: jboolean,
    menu: jboolean,
) -> jboolean {
    if path.is_null() {
        return 0;
    }
    if let Ok(p) = env.get_string(&path) {
        let rust_path = p.to_string_lossy();
        if sync_extra_options_ini(
            &rust_path,
            regional != 0,
            lap_ctrl != 0,
            opp_ctrl != 0,
            lap_pct != 0,
            drift != 0,
            fixes != 0,
            menu != 0,
        ) {
            1
        } else {
            0
        }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GameOptions_nativeDetectRegion(
    mut env: JNIEnv,
    _class: JClass,
    game_root: JString,
) -> jint {
    if game_root.is_null() {
        return -1;
    }
    if let Ok(r) = env.get_string(&game_root) {
        let rust_root = r.to_string_lossy();
        detect_region(&[&rust_root, "/sdcard/nfsu2", "/sdcard/NFSU2"]) as jint
    } else {
        -1
    }
}
