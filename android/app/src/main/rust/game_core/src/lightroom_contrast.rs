//! Pure Rust Lightroom-based Enhanced Contrast Engine for NFSU2 Android.
//!
//! Implements professional Lightroom photographic tone reproduction logic:
//! - EV: +0.01 (Exposure Value)
//! - Contrast: +15
//! - Highlight: -35 (High-tone glare recovery & neon definition)
//! - Shadow: -12 (Deep shadow richness)
//! - White: +16 (Specular highlight peak boost)
//! - Black: +22 (Near-black detail lift with true black anchoring)
//! - Dehaze: +20 (Atmospheric clarity and midtone saturation boost)
//!
//! Provides zero-overhead GPU tone ramp generation and per-pixel color mapping.

use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

/// Photographic tone curve parameters matching Adobe Lightroom specifications.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct LightroomParams {
    pub ev: f32,         // Exposure value bias (+0.01)
    pub contrast: f32,   // Tone contrast (+15.0)
    pub highlight: f32,  // Highlight recovery (-35.0)
    pub shadow: f32,     // Shadow depth (-12.0)
    pub white: f32,      // Specular white peak (+16.0)
    pub black: f32,      // Black point detail lift (+22.0)
    pub dehaze: f32,     // Dehaze atmospheric clarity (+20.0)
}

impl Default for LightroomParams {
    fn default() -> Self {
        Self {
            ev: 0.01,
            contrast: 15.0,
            highlight: -35.0,
            shadow: -12.0,
            white: 16.0,
            black: 22.0,
            dehaze: 20.0,
        }
    }
}

pub static GLOBAL_LIGHTROOM_PARAMS: LightroomParams = LightroomParams {
    ev: 0.01,
    contrast: 15.0,
    highlight: -35.0,
    shadow: -12.0,
    white: 16.0,
    black: 22.0,
    dehaze: 20.0,
};

static INGAME_CONTRAST_ENABLED: AtomicBool = AtomicBool::new(true);
static PROCESSED_FRAMES: AtomicU32 = AtomicU32::new(0);

#[inline]
fn smoothstep(edge0: f32, edge1: f32, x: f32) -> f32 {
    let t = ((x - edge0) / (edge1 - edge0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// Applies Lightroom photographic tone curve to a single normalized scalar luminance channel [0.0, 1.0].
#[inline]
pub fn lightroom_tone_curve(x: f32, p: &LightroomParams) -> f32 {
    let mut c = x.clamp(0.0, 1.0);

    // 1. Exposure Value: EV +0.01 (2^0.01 ~= 1.00695555)
    let ev_scale = 2.0f32.powf(p.ev);
    c = (c * ev_scale).clamp(0.0, 1.0);

    // 2. Contrast: +15 (Sigmoidal S-curve blend)
    let s_curve = c * c * (3.0 - 2.0 * c);
    let contrast_factor = (p.contrast / 100.0).clamp(-1.0, 1.0);
    c = c * (1.0 - contrast_factor) + s_curve * contrast_factor;

    // 3. Highlight: -35 (Compresses upper tones > 0.5 to restore blown-out neon/sky detail)
    let th = smoothstep(0.5, 1.0, c);
    let hl_factor = p.highlight / 100.0; // -0.35
    c += hl_factor * 0.20 * th * (c - 0.5);

    // 4. White: +16 (Raises peak specular highlights > 0.75 for night reflections)
    let tw = smoothstep(0.75, 1.0, c);
    let wh_factor = p.white / 100.0; // +0.16
    c += wh_factor * 0.15 * tw * (1.0 - c);

    // 5. Shadow: -12 (Deepens dark tones < 0.5 for rich asphalt and night contrast)
    let ts = smoothstep(0.5, 0.0, c);
    let sh_factor = p.shadow / 100.0; // -0.12
    c += sh_factor * 0.18 * ts * (0.5 - c);

    // 6. Black: +22 (Lifts near-black details without washing true blacks)
    let tb = smoothstep(0.25, 0.0, c);
    let bk_factor = p.black / 100.0; // +0.22
    c += bk_factor * 0.10 * tb * c;

    // 7. Dehaze: +20 (Local atmospheric clarity in midtones)
    let dh_factor = p.dehaze / 100.0; // +0.20
    c -= dh_factor * 0.12 * (1.0 - c) * c;

    c.clamp(0.0, 1.0)
}

/// Applies Lightroom photographic tone reproduction to full RGB with dehaze chroma boost.
#[inline]
pub fn lightroom_process_rgb(r: f32, g: f32, b: f32, p: &LightroomParams) -> (f32, f32, f32) {
    let r_toned = lightroom_tone_curve(r, p);
    let g_toned = lightroom_tone_curve(g, p);
    let b_toned = lightroom_tone_curve(b, p);

    // Dehaze chroma boost (+20% saturation to illuminate night lighting and neons)
    let luma = 0.2126 * r_toned + 0.7152 * g_toned + 0.0722 * b_toned;
    let sat_boost = 1.0 + (p.dehaze / 100.0); // 1.20
    let r_final = (luma + (r_toned - luma) * sat_boost).clamp(0.0, 1.0);
    let g_final = (luma + (g_toned - luma) * sat_boost).clamp(0.0, 1.0);
    let b_final = (luma + (b_toned - luma) * sat_boost).clamp(0.0, 1.0);

    (r_final, g_final, b_final)
}

/// Generates a 256-entry 16-bit hardware gamma ramp for Direct3D 9 SetGammaRamp.
pub fn generate_lightroom_ramp(p: &LightroomParams) -> [u16; 256] {
    let mut ramp = [0u16; 256];
    for i in 0..256 {
        let x = i as f32 / 255.0;
        let y = lightroom_tone_curve(x, p);
        ramp[i] = (y * 65535.0).round() as u16;
    }
    ramp
}

// ---------------------------------------------------------------------------
// C-ABI Exports for runtime linking & D3D9 bridge
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_lightroom_get_ramp(out_ramp: *mut u16) -> i32 {
    if out_ramp.is_null() {
        return -1;
    }
    let ramp = generate_lightroom_ramp(&GLOBAL_LIGHTROOM_PARAMS);
    std::ptr::copy_nonoverlapping(ramp.as_ptr(), out_ramp, 256);
    0
}

#[no_mangle]
pub extern "C" fn nfs_lightroom_process_pixel(r: f32, g: f32, b: f32, out_rgb: *mut f32) {
    if out_rgb.is_null() {
        return;
    }
    let (ro, go, bo) = lightroom_process_rgb(r, g, b, &GLOBAL_LIGHTROOM_PARAMS);
    unsafe {
        *out_rgb.add(0) = ro;
        *out_rgb.add(1) = go;
        *out_rgb.add(2) = bo;
    }
}

#[no_mangle]
pub extern "C" fn nfs_lightroom_set_enabled(enabled: i32) {
    INGAME_CONTRAST_ENABLED.store(enabled != 0, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn nfs_lightroom_is_enabled() -> i32 {
    if INGAME_CONTRAST_ENABLED.load(Ordering::Relaxed) { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn nfs_lightroom_frame_tick() {
    PROCESSED_FRAMES.fetch_add(1, Ordering::Relaxed);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lightroom_boundaries() {
        let p = LightroomParams::default();
        let black = lightroom_tone_curve(0.0, &p);
        let white = lightroom_tone_curve(1.0, &p);
        assert_eq!(black, 0.0, "Black point must remain 0.0");
        assert_eq!(white, 1.0, "White point must remain 1.0");
    }

    #[test]
    fn test_lightroom_ramp_monotonicity() {
        let p = LightroomParams::default();
        let ramp = generate_lightroom_ramp(&p);
        assert_eq!(ramp[0], 0);
        assert_eq!(ramp[255], 65535);
        for i in 1..256 {
            assert!(ramp[i] >= ramp[i - 1], "Ramp should be monotonic at index {}", i);
        }
    }

    #[test]
    fn test_lightroom_rgb_processing() {
        let p = LightroomParams::default();
        let (r, g, b) = lightroom_process_rgb(0.5, 0.5, 0.5, &p);
        assert!(r > 0.0 && r < 1.0);
        assert_eq!(r, g);
        assert_eq!(g, b);
    }
}
