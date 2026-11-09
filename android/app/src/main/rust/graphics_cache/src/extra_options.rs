//! Native Extra Options mod port for NFSU2 Android in Rust.
//!
//! Ported from ExOptsTeam/NFSU2ExOpts (https://github.com/ExOptsTeam/NFSU2ExOpts).
//! Provides INI configuration parsing, memory patch generation, and runtime state
//! management (including Dumb AI / AutoDrive and Starting Cash) without unlockable cheats.

use std::collections::HashMap;
use std::ffi::CStr;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::os::raw::{c_char, c_int};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::RwLock;

/// Drift multiplier score thresholds for multipliers 2x through 9x across 4 difficulty tiers.
pub const DRIFT_MULTIPLIER_THRESHOLDS: [u32; 32] = [
    350, 1400, 4200, 11200, 22400, 38080, 57120, 85680,
    300, 1200, 3600, 9600, 19200, 32640, 48960, 73440,
    250, 1000, 3000, 8000, 16000, 27200, 40800, 61200,
    200, 800, 2400, 6400, 12800, 21760, 32640, 48960,
];

#[derive(Debug, Clone)]
pub struct ExtraOptionsSettings {
    // LapControllers
    pub min_lap: u8,
    pub max_lap: u8,
    pub ko_lap: u8,

    // OpponentControllers
    pub min_cpu: u8,
    pub max_cpu: u8,
    pub ko_cpu: u8,
    pub max_players_lan: u8,

    // Drift
    pub max_drift_multiplier: u8,
    pub drift_plus: bool,
    pub drift_multiply: bool,
    pub hide_nifty_messages: bool,

    // Menu
    pub show_outrun: bool,
    pub show_more_race_options: u8,
    pub free_run_track_select: bool,
    pub outrun_track_select: bool,
    pub splash_screen_time_limit: f32,
    pub allow_longer_profile_names: bool,

    // Gameplay
    pub headlights_mode: u8,
    pub low_beam_brightness: f32,
    pub high_beam_brightness: f32,
    pub show_percent_on_1lap_races: bool,
    pub unlock_regional_cars: bool,
    pub neon_brightness: f32,
    pub nos_trail_repeat_count: i32,
    pub game_speed: f32,
    pub world_animation_speed: f32,
    pub starting_cash: u32,
    pub dumb_ai: bool,

    // Fixes
    pub disappearing_wheels_fix: bool,
    pub damage_fix: bool,
    pub megalow_parts_fix: bool,

    // Weather
    pub always_rain: bool,
    pub general_rain_amount: f32,
    pub road_reflection: f32,
    pub rain_size: f32,
    pub rain_intensity: f32,
    pub rain_crossing: f32,
    pub rain_speed: f32,
    pub rain_gravity: f32,
}

impl Default for ExtraOptionsSettings {
    fn default() -> Self {
        Self {
            min_lap: 0,
            max_lap: 127,
            ko_lap: 5,
            min_cpu: 0,
            max_cpu: 5,
            ko_cpu: 5,
            max_players_lan: 6,
            max_drift_multiplier: 5,
            drift_plus: false,
            drift_multiply: false,
            hide_nifty_messages: false,
            show_outrun: false,
            show_more_race_options: 1,
            free_run_track_select: true,
            outrun_track_select: true,
            splash_screen_time_limit: 30.0,
            allow_longer_profile_names: true,
            headlights_mode: 2,
            low_beam_brightness: 0.75,
            high_beam_brightness: 1.0,
            show_percent_on_1lap_races: true,
            unlock_regional_cars: true,
            neon_brightness: 1.0,
            nos_trail_repeat_count: 8,
            game_speed: 1.0,
            world_animation_speed: 45.0,
            starting_cash: 0,
            dumb_ai: false,
            disappearing_wheels_fix: true,
            damage_fix: true,
            megalow_parts_fix: true,
            always_rain: false,
            general_rain_amount: 1.0,
            road_reflection: 1.0,
            rain_size: 0.03,
            rain_intensity: 0.7,
            rain_crossing: 0.02,
            rain_speed: 0.03,
            rain_gravity: 0.35,
        }
    }
}

pub struct IniParser {
    sections: HashMap<String, HashMap<String, String>>,
}

impl IniParser {
    pub fn new() -> Self {
        Self {
            sections: HashMap::new(),
        }
    }

    pub fn load_file<P: AsRef<Path>>(&mut self, path: P) -> bool {
        let file = match File::open(path) {
            Ok(f) => f,
            Err(_) => return false,
        };

        let reader = BufReader::new(file);
        let mut current_section = String::new();

        for line in reader.lines().flatten() {
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with(';') || trimmed.starts_with('#') {
                continue;
            }

            // Remove trailing inline comments
            let content = if let Some(idx) = trimmed.find("//") {
                &trimmed[..idx]
            } else if let Some(idx) = trimmed.find(';') {
                &trimmed[..idx]
            } else if let Some(idx) = trimmed.find('#') {
                &trimmed[..idx]
            } else {
                trimmed
            }
            .trim();

            if content.starts_with('[') && content.ends_with(']') {
                current_section = content[1..content.len() - 1].trim().to_lowercase();
                self.sections.entry(current_section.clone()).or_default();
            } else if let Some(eq_pos) = content.find('=') {
                let key = content[..eq_pos].trim().to_lowercase();
                let val = content[eq_pos + 1..].trim().to_string();
                if !current_section.is_empty() && !key.is_empty() {
                    self.sections
                        .entry(current_section.clone())
                        .or_default()
                        .insert(key, val);
                }
            }
        }
        true
    }

    pub fn get_str(&self, section: &str, key: &str, default: &str) -> String {
        let sec = section.to_lowercase();
        let k = key.to_lowercase();
        self.sections
            .get(&sec)
            .and_then(|s| s.get(&k))
            .cloned()
            .unwrap_or_else(|| default.to_string())
    }

    pub fn get_int<T: std::str::FromStr>(&self, section: &str, key: &str, default: T) -> T {
        let val_str = self.get_str(section, key, "");
        val_str.parse::<T>().unwrap_or(default)
    }

    pub fn get_bool(&self, section: &str, key: &str, default: bool) -> bool {
        let val_str = self.get_str(section, key, if default { "1" } else { "0" });
        val_str == "1" || val_str.eq_ignore_ascii_case("true")
    }

    pub fn get_float(&self, section: &str, key: &str, default: f32) -> f32 {
        let val_str = self.get_str(section, key, "");
        val_str.parse::<f32>().unwrap_or(default)
    }
}

pub fn load_extra_options_ini(game_root: Option<&str>) -> ExtraOptionsSettings {
    let mut parser = IniParser::new();
    let mut loaded = false;

    // Check /sdcard/cache/scripts/NFSU2ExtraOptionsSettings.ini
    let sdcard_path = "/sdcard/cache/scripts/NFSU2ExtraOptionsSettings.ini";
    if Path::new(sdcard_path).exists() {
        loaded = parser.load_file(sdcard_path);
    }

    // Fall back to <game_root>/scripts/NFSU2ExtraOptionsSettings.ini
    if !loaded {
        if let Some(root) = game_root {
            let candidate = format!("{}/scripts/NFSU2ExtraOptionsSettings.ini", root.trim_end_matches('/'));
            if Path::new(&candidate).exists() {
                loaded = parser.load_file(&candidate);
            }
        }
    }

    if !loaded {
        return ExtraOptionsSettings::default();
    }

    let mut cfg = ExtraOptionsSettings::default();

    // LapControllers
    cfg.min_lap = parser.get_int("LapControllers", "Minimum", cfg.min_lap) % 128;
    cfg.max_lap = parser.get_int("LapControllers", "Maximum", cfg.max_lap) % 128;
    cfg.ko_lap = parser.get_int("LapControllers", "KOEnabled", cfg.ko_lap) % 128;

    // OpponentControllers
    cfg.min_cpu = parser.get_int("OpponentControllers", "Minimum", cfg.min_cpu) % 6;
    cfg.max_cpu = parser.get_int("OpponentControllers", "Maximum", cfg.max_cpu) % 6;
    cfg.ko_cpu = parser.get_int("OpponentControllers", "KOEnabled", cfg.ko_cpu) % 6;
    cfg.max_players_lan = parser.get_int("OpponentControllers", "MaximumLANPlayers", cfg.max_players_lan)
        .clamp(2, 6);

    // Drift
    let max_drift = parser.get_int("Drift", "MaximumMultiplier", cfg.max_drift_multiplier);
    cfg.max_drift_multiplier = if max_drift == 0 { 1 } else { max_drift.min(9) };
    cfg.drift_plus = parser.get_bool("Drift", "PlusSign", cfg.drift_plus);
    cfg.drift_multiply = parser.get_bool("Drift", "ShowWithoutMultiplying", cfg.drift_multiply);
    cfg.hide_nifty_messages = parser.get_bool("Drift", "HideNiftyDriftMessages", cfg.hide_nifty_messages);

    // Menu
    cfg.show_outrun = parser.get_bool("Menu", "ShowOutrun", cfg.show_outrun);
    cfg.show_more_race_options = parser.get_int("Menu", "ShowMoreRaceOptions", cfg.show_more_race_options);
    cfg.free_run_track_select = parser.get_bool("Menu", "FreeRunTrackSelect", cfg.free_run_track_select);
    cfg.outrun_track_select = parser.get_bool("Menu", "OutrunTrackSelect", cfg.outrun_track_select);
    cfg.splash_screen_time_limit = parser.get_float("Menu", "SplashScreenTimeLimit", cfg.splash_screen_time_limit);
    cfg.allow_longer_profile_names = parser.get_bool("Menu", "AllowLongerProfileNames", cfg.allow_longer_profile_names);

    // Gameplay
    cfg.headlights_mode = parser.get_int("Gameplay", "HeadlightsMode", cfg.headlights_mode) % 3;
    cfg.low_beam_brightness = parser.get_float("Gameplay", "LowBeamBrightness", cfg.low_beam_brightness);
    cfg.high_beam_brightness = parser.get_float("Gameplay", "HighBeamBrightness", cfg.high_beam_brightness);
    cfg.show_percent_on_1lap_races = parser.get_bool("Gameplay", "ShowPercentOn1LapRaces", cfg.show_percent_on_1lap_races);
    cfg.unlock_regional_cars = parser.get_bool("Gameplay", "UnlockRegionalCars", cfg.unlock_regional_cars);
    cfg.neon_brightness = parser.get_float("Gameplay", "NeonBrightness", cfg.neon_brightness);
    cfg.nos_trail_repeat_count = parser.get_int("Gameplay", "NosTrailRepeatCount", cfg.nos_trail_repeat_count);
    cfg.game_speed = parser.get_float("Gameplay", "GameSpeed", cfg.game_speed);
    cfg.world_animation_speed = parser.get_float("Gameplay", "WorldAnimationSpeed", cfg.world_animation_speed);
    cfg.starting_cash = parser.get_int("Gameplay", "StartingCash", 0);

    // Fixes
    cfg.disappearing_wheels_fix = parser.get_bool("Fixes", "DisappearingWheelsFix", cfg.disappearing_wheels_fix);
    cfg.damage_fix = parser.get_bool("Fixes", "DamageFix", cfg.damage_fix);
    cfg.megalow_parts_fix = parser.get_bool("Fixes", "MegalowPartsFix", cfg.megalow_parts_fix);

    // Weather
    cfg.always_rain = parser.get_bool("Weather", "AlwaysRain", cfg.always_rain);
    cfg.general_rain_amount = parser.get_float("Weather", "GeneralRainAmount", cfg.general_rain_amount);
    cfg.road_reflection = parser.get_float("Weather", "RoadReflectionAmount", cfg.road_reflection);
    cfg.rain_size = parser.get_float("Weather", "RainSize", cfg.rain_size);
    cfg.rain_intensity = parser.get_float("Weather", "RainIntensity", cfg.rain_intensity);
    cfg.rain_crossing = parser.get_float("Weather", "RainCrossing", cfg.rain_crossing);
    cfg.rain_speed = parser.get_float("Weather", "RainSpeed", cfg.rain_speed);
    cfg.rain_gravity = parser.get_float("Weather", "RainGravity", cfg.rain_gravity);

    cfg
}

#[derive(Debug, Clone)]
pub struct PatchEntry {
    pub va: u32,
    pub bytes: Vec<u8>,
}

/// Generates all memory patches according to configuration.
pub fn generate_patches(cfg: &ExtraOptionsSettings) -> Vec<PatchEntry> {
    let mut patches = Vec::new();

    // 1. Optical drive / Disc 2 bypass
    patches.push(PatchEntry { va: 0x005C0D3F, bytes: vec![0x78] });
    patches.push(PatchEntry { va: 0x005C0D54, bytes: vec![0x00] });

    // Ground Shadow Alignment Fix (0.015f instead of 0.04f to sit flush with road and prevent wheel clipping)
    patches.push(PatchEntry { va: 0x007A66B4, bytes: 0.015f32.to_le_bytes().to_vec() });

    // 2. LapControllers
    patches.push(PatchEntry { va: 0x004B3D82, bytes: vec![cfg.min_lap] });
    patches.push(PatchEntry { va: 0x004B3D9B, bytes: vec![cfg.min_lap] });
    patches.push(PatchEntry { va: 0x004B3D86, bytes: vec![cfg.max_lap] });
    patches.push(PatchEntry { va: 0x004B3D97, bytes: vec![cfg.max_lap] });
    patches.push(PatchEntry { va: 0x004B407F, bytes: vec![cfg.ko_lap] });

    // LAN Laps
    patches.push(PatchEntry { va: 0x004B3E7A, bytes: vec![cfg.min_lap] });
    patches.push(PatchEntry { va: 0x004B3EE4, bytes: vec![cfg.min_lap] });
    patches.push(PatchEntry { va: 0x004B3E9E, bytes: vec![cfg.min_lap] });
    patches.push(PatchEntry { va: 0x004B3F30, bytes: vec![cfg.max_lap] });
    patches.push(PatchEntry { va: 0x004B3F41, bytes: vec![cfg.max_lap] });

    // 3. OpponentControllers
    patches.push(PatchEntry { va: 0x004B3867, bytes: vec![0x90, 0x90, 0x90, 0x90] });
    patches.push(PatchEntry { va: 0x004B3863, bytes: vec![cfg.min_cpu] });
    patches.push(PatchEntry { va: 0x004B382B, bytes: vec![0x90, 0x90, 0x90, 0x90, 0x90, 0x90] });
    patches.push(PatchEntry { va: 0x004B3834, bytes: vec![cfg.max_cpu] });
    patches.push(PatchEntry { va: 0x004B4075, bytes: vec![cfg.ko_cpu] });

    // LAN Players
    patches.push(PatchEntry { va: 0x004B31AA, bytes: vec![0xB0] });
    patches.push(PatchEntry { va: 0x004B31AB, bytes: vec![cfg.max_players_lan] });
    patches.push(PatchEntry { va: 0x004B31AC, bytes: vec![0x90, 0x90] });

    // 4. ShowMoreRaceOptions (Unfreeze KO)
    if cfg.show_more_race_options >= 1 {
        patches.push(PatchEntry { va: 0x004B4068, bytes: vec![0xEB] });
        patches.push(PatchEntry { va: 0x004B3E14, bytes: vec![0xEB] });
        patches.push(PatchEntry { va: 0x004B3903, bytes: vec![0xEB] });
    }
    if cfg.show_more_race_options >= 2 {
        patches.push(PatchEntry { va: 0x004CD8D8, bytes: vec![0x90, 0x90] });
        patches.push(PatchEntry { va: 0x0053F8B0, bytes: vec![0xEB] });
    }

    // 5. Regional Cars (Peugeot 106 & Opel Corsa in NTSC)
    if cfg.unlock_regional_cars {
        patches.push(PatchEntry { va: 0x007F7C48, bytes: vec![0xFF, 0xFF, 0xFF, 0xFF] });
        patches.push(PatchEntry { va: 0x007F7C4C, bytes: vec![0xFF, 0xFF, 0xFF, 0xFF] });
        patches.push(PatchEntry { va: 0x007F7C50, bytes: vec![0xFF, 0xFF, 0xFF, 0xFF] });
        patches.push(PatchEntry { va: 0x007F7C54, bytes: vec![0xFF, 0xFF, 0xFF, 0xFF] });
        patches.push(PatchEntry { va: 0x004FEB9D, bytes: vec![0x1C] });
        patches.push(PatchEntry { va: 0x007F6DBE, bytes: vec![0x07] });
        patches.push(PatchEntry { va: 0x007F6DBF, bytes: vec![0x06] });
    }

    // 6. ShowPercentOn1LapRaces (Carbon-style progress)
    if cfg.show_percent_on_1lap_races {
        patches.push(PatchEntry { va: 0x004AA27F, bytes: vec![0xBC] });
        patches.push(PatchEntry { va: 0x004AA283, bytes: vec![0x3C] });
        patches.push(PatchEntry { va: 0x004AA284, bytes: vec![0x01] });
        patches.push(PatchEntry { va: 0x004AA286, bytes: vec![0x85] });
    }

    // 7. Track Select options
    if cfg.free_run_track_select {
        patches.push(PatchEntry { va: 0x004B2AE3, bytes: vec![0x90, 0x90] });
    }
    if cfg.outrun_track_select {
        patches.push(PatchEntry { va: 0x004B2AA0, bytes: vec![0x90, 0x90] });
        patches.push(PatchEntry { va: 0x005268F8, bytes: vec![0x90, 0x90] });
    }
    if cfg.show_outrun {
        patches.push(PatchEntry { va: 0x004B2F3E, bytes: vec![0x90, 0x90] });
    }

    // 8. Drift settings
    if cfg.drift_multiply {
        patches.push(PatchEntry { va: 0x004AB792, bytes: vec![0xB0, 0x01, 0x90] });
    }
    if cfg.drift_plus {
        patches.push(PatchEntry { va: 0x004AB7DB, bytes: vec![0x74] });
    }
    if cfg.hide_nifty_messages {
        patches.push(PatchEntry { va: 0x004AC136, bytes: vec![0xEB] });
    }
    patches.push(PatchEntry { va: 0x0056CC58, bytes: vec![cfg.max_drift_multiplier] });
    patches.push(PatchEntry { va: 0x0056CC90, bytes: vec![cfg.max_drift_multiplier] });
    patches.push(PatchEntry { va: 0x0056CC98, bytes: vec![0xCF] });
    patches.push(PatchEntry { va: 0x0056CC9C, bytes: vec![0x00, 0x50, 0x92] });

    // Drift threshold points table at 0x00925004
    let mut drift_table_bytes = Vec::with_capacity(32 * 4);
    for pts in DRIFT_MULTIPLIER_THRESHOLDS.iter() {
        drift_table_bytes.extend_from_slice(&pts.to_le_bytes());
    }
    patches.push(PatchEntry { va: 0x00925004, bytes: drift_table_bytes });

    let drift_scale = (cfg.max_drift_multiplier as f32) / 5.0f32;
    patches.push(PatchEntry { va: 0x00925100, bytes: drift_scale.to_le_bytes().to_vec() });

    // 9. Profile Names
    if cfg.allow_longer_profile_names {
        patches.push(PatchEntry { va: 0x00556517, bytes: vec![15] });
        patches.push(PatchEntry { va: 0x0055651B, bytes: vec![0] });
        patches.push(PatchEntry { va: 0x004E910A, bytes: vec![16] });
        patches.push(PatchEntry { va: 0x004E910E, bytes: vec![b'/'] });
    }

    // 10. Headlights & Visuals
    let headlight_brightness = match cfg.headlights_mode {
        0 => 0.0f32,
        1 => cfg.low_beam_brightness,
        _ => cfg.high_beam_brightness,
    };
    patches.push(PatchEntry { va: 0x00615950, bytes: headlight_brightness.to_le_bytes().to_vec() });
    let flare_byte = if cfg.headlights_mode == 0 { 0x00 } else { b'H' };
    patches.push(PatchEntry { va: 0x007A3658, bytes: vec![flare_byte] });

    patches.push(PatchEntry { va: 0x0060D8D0, bytes: cfg.neon_brightness.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x00802920, bytes: cfg.nos_trail_repeat_count.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x00803A7C, bytes: cfg.world_animation_speed.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x0061F1F2, bytes: vec![0x90, 0x90, 0x90, 0x90, 0x90, 0x90] });
    patches.push(PatchEntry { va: 0x007A5730, bytes: cfg.game_speed.to_le_bytes().to_vec() });

    // 11. Fixes
    if cfg.disappearing_wheels_fix {
        patches.push(PatchEntry { va: 0x0060C5A9, bytes: vec![0x01] });
    }
    if cfg.damage_fix {
        patches.push(PatchEntry { va: 0x0059FC13, bytes: vec![0xEB] });
    }
    if cfg.megalow_parts_fix {
        let neg1 = (-1i32).to_le_bytes().to_vec();
        patches.push(PatchEntry { va: 0x004C4D87, bytes: neg1.clone() });
        patches.push(PatchEntry { va: 0x0052442A, bytes: neg1.clone() });
        patches.push(PatchEntry { va: 0x0053043A, bytes: neg1.clone() });
        patches.push(PatchEntry { va: 0x0054163B, bytes: neg1.clone() });
        patches.push(PatchEntry { va: 0x00556F1D, bytes: neg1.clone() });
        patches.push(PatchEntry { va: 0x005627BD, bytes: neg1 });
    }

    // 12. Weather
    if cfg.always_rain {
        patches.push(PatchEntry { va: 0x008A1D38, bytes: vec![0x01] });
    }
    patches.push(PatchEntry { va: 0x00803968, bytes: cfg.general_rain_amount.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x00803AC8, bytes: cfg.road_reflection.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x008039DC, bytes: cfg.rain_size.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x008039E0, bytes: cfg.rain_intensity.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x00803970, bytes: cfg.rain_crossing.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x00803974, bytes: cfg.rain_speed.to_le_bytes().to_vec() });
    patches.push(PatchEntry { va: 0x00803978, bytes: cfg.rain_gravity.to_le_bytes().to_vec() });

    // 13. Starting Cash
    if cfg.starting_cash > 0 {
        patches.push(PatchEntry { va: 0x00532913 - 4, bytes: cfg.starting_cash.to_le_bytes().to_vec() });
    }

    patches
}

// ---------------------------------------------------------------------------
// Global State & Runtime Exports
// ---------------------------------------------------------------------------

static G_EXTRA_OPTIONS: RwLock<Option<ExtraOptionsSettings>> = RwLock::new(None);
static G_DUMB_AI: AtomicBool = AtomicBool::new(false);
static G_PATCHES: RwLock<Vec<PatchEntry>> = RwLock::new(Vec::new());

#[no_mangle]
pub unsafe extern "C" fn nfs_extra_options_init(
    game_root: *const c_char,
    dumb_ai_cheat: c_int,
    starting_cash_cheat: c_int,
) -> c_int {
    let root_str = if !game_root.is_null() {
        CStr::from_ptr(game_root).to_str().ok()
    } else {
        None
    };

    let mut cfg = load_extra_options_ini(root_str);

    // Apply Launcher cheat overrides
    if dumb_ai_cheat != 0 {
        cfg.dumb_ai = true;
    }
    if starting_cash_cheat != 0 && cfg.starting_cash == 0 {
        cfg.starting_cash = 20000;
    }

    G_DUMB_AI.store(cfg.dumb_ai, Ordering::Relaxed);

    let patches = generate_patches(&cfg);
    let patch_count = patches.len();

    if let Ok(mut lock) = G_PATCHES.write() {
        *lock = patches;
    }
    if let Ok(mut lock) = G_EXTRA_OPTIONS.write() {
        *lock = Some(cfg);
    }

    patch_count as c_int
}

#[no_mangle]
pub extern "C" fn nfs_extra_options_patch_count() -> usize {
    G_PATCHES.read().map(|p| p.len()).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_extra_options_get_patch(
    index: usize,
    out_va: *mut u32,
    out_len: *mut usize,
) -> *const u8 {
    if out_va.is_null() || out_len.is_null() {
        return std::ptr::null();
    }
    if let Ok(patches) = G_PATCHES.read() {
        if let Some(entry) = patches.get(index) {
            *out_va = entry.va;
            *out_len = entry.bytes.len();
            return entry.bytes.as_ptr();
        }
    }
    *out_va = 0;
    *out_len = 0;
    std::ptr::null()
}

#[no_mangle]
pub extern "C" fn nfs_extra_options_is_dumb_ai_enabled() -> c_int {
    if G_DUMB_AI.load(Ordering::Relaxed) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn nfs_extra_options_set_dumb_ai_enabled(enabled: c_int) {
    G_DUMB_AI.store(enabled != 0, Ordering::Relaxed);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_patches() {
        let cfg = ExtraOptionsSettings::default();
        let patches = generate_patches(&cfg);
        assert!(!patches.is_empty());
        // Verify CD check bypass is present
        assert!(patches.iter().any(|p| p.va == 0x005C0D3F));
        // Verify Regional Cars are present
        assert!(patches.iter().any(|p| p.va == 0x007F7C48));
    }
}
