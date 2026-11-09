//! 3D Visual Geometry, Stance Transformations & Stream Pipeline Acceleration in Rust.
//!
//! Provides:
//! - 128-bit SIMD accelerated vertex/index buffer memory streaming to bypass cache bottlenecks
//! - 3D wheel stance transformations (camber matrix rotation Rx(alpha) and track width offsets)
//! - Dyno tuning setup record persistence
//! - Drift event camera preference preservation
//! - Dynamic gearbox straight-cut transmission whine pitch/volume synthesis

#[cfg(target_arch = "aarch64")]
use core::arch::aarch64::*;

// ---------------------------------------------------------------------------
// 1. 128-bit NEON SIMD Stream Copy for Vertex / Index Buffers
// ---------------------------------------------------------------------------

/// High-throughput aligned stream copy using 128-bit NEON vectors.
/// Eliminates CPU cache pollution when uploading dynamic geometry on mobile SoCs.
#[no_mangle]
pub unsafe extern "C" fn nfs_simd_stream_copy(
    dst: *mut u8,
    src: *const u8,
    bytes: usize,
) {
    if dst.is_null() || src.is_null() || bytes == 0 {
        return;
    }

    #[cfg(target_arch = "aarch64")]
    {
        let mut offset = 0usize;
        let chunk_size = 64usize; // 4x 128-bit vectors

        while offset + chunk_size <= bytes {
            let v0 = vld1q_u8(src.add(offset));
            let v1 = vld1q_u8(src.add(offset + 16));
            let v2 = vld1q_u8(src.add(offset + 32));
            let v3 = vld1q_u8(src.add(offset + 48));

            vst1q_u8(dst.add(offset), v0);
            vst1q_u8(dst.add(offset + 16), v1);
            vst1q_u8(dst.add(offset + 32), v2);
            vst1q_u8(dst.add(offset + 48), v3);

            offset += chunk_size;
        }

        while offset + 16 <= bytes {
            let v = vld1q_u8(src.add(offset));
            vst1q_u8(dst.add(offset), v);
            offset += 16;
        }

        if offset < bytes {
            std::ptr::copy_nonoverlapping(src.add(offset), dst.add(offset), bytes - offset);
        }
    }

    #[cfg(not(target_arch = "aarch64"))]
    {
        std::ptr::copy_nonoverlapping(src, dst, bytes);
    }
}

// ---------------------------------------------------------------------------
// 2. 3D Stance Transformation & Camber Rotation Matrix
// ---------------------------------------------------------------------------

/// Computes a 4x4 row-major rotation matrix for wheel camber angle (Rx(alpha))
/// combined with sideways wheel offset and tire width scale.
#[no_mangle]
pub unsafe extern "C" fn nfs_visual_compute_stance_matrix(
    camber_deg: f32,
    track_offset: f32,
    tire_width_scale: f32,
    out_matrix: *mut f32,
) {
    if out_matrix.is_null() {
        return;
    }

    let rad = camber_deg * (std::f32::consts::PI / 180.0);
    let cos_a = rad.cos();
    let sin_a = rad.sin();

    let m = std::slice::from_raw_parts_mut(out_matrix, 16);

    // Row 0: X axis (scaled by tire width)
    m[0] = tire_width_scale;
    m[1] = 0.0;
    m[2] = 0.0;
    m[3] = 0.0;

    // Row 1: Y axis (Rx rotation)
    m[4] = 0.0;
    m[5] = cos_a;
    m[6] = -sin_a;
    m[7] = 0.0;

    // Row 2: Z axis (Rx rotation)
    m[8] = 0.0;
    m[9] = sin_a;
    m[10] = cos_a;
    m[11] = 0.0;

    // Row 3: Translation offset (track offset on X)
    m[12] = track_offset;
    m[13] = 0.0;
    m[14] = 0.0;
    m[15] = 1.0;
}

// ---------------------------------------------------------------------------
// 3. Dyno Tuning Setup Preservation
// ---------------------------------------------------------------------------

/// Constants matching NFSU2 Dyno object layout
const DYNO_RECORD_OFFSET: usize = 0x000;
const DYNO_TUNED_OFFSET: usize = 0x900;
const DYNO_KIND_OFFSET: usize = 0x9FC;
const REC_SETUPS_OFFSET: usize = 0x360;
const SETUP_SIZE: usize = 0x29; // 41 bytes per custom tuning setup

/// Stores the active tuned setup into the car record before saving profile.
/// Returns 1 if updated, 0 otherwise.
#[no_mangle]
pub unsafe extern "C" fn nfs_visual_dyno_store_setups(
    dyno_obj: *mut u8,
    msg_id: u32,
    save_option: u8,
) -> i32 {
    if dyno_obj.is_null() {
        return 0;
    }

    const MSG_QUIT_YES: u32 = 0x30ED2368;
    const MSG_SAVE_YES: u32 = 0x51123592;

    if msg_id != MSG_SAVE_YES && (msg_id != MSG_QUIT_YES || save_option != 0) {
        return 0;
    }

    let record_ptr = *(dyno_obj.add(DYNO_RECORD_OFFSET) as *const *mut u8);
    let tuned_ptr = *(dyno_obj.add(DYNO_TUNED_OFFSET) as *const *const u8);
    let kind = *(dyno_obj.add(DYNO_KIND_OFFSET) as *const u32);

    if record_ptr.is_null() || tuned_ptr.is_null() || kind >= 5 {
        return 0;
    }

    let dst = record_ptr.add(REC_SETUPS_OFFSET + (kind as usize) * SETUP_SIZE);
    std::ptr::copy_nonoverlapping(tuned_ptr, dst, SETUP_SIZE);

    1
}

// ---------------------------------------------------------------------------
// 4. Drift Event Camera Preservation
// ---------------------------------------------------------------------------

static mut G_SAVED_CAMERA_VIEW: u32 = 0; // 0 = default rear near
static mut G_CAMERA_KNOWN: bool = false;

/// Preserves the player's camera view after completing a drift event,
/// preventing the game from resetting view 4 (drift cam) to view 3.
#[no_mangle]
pub extern "C" fn nfs_visual_camera_on_setup(current_view: u32, is_drift_race: i32) -> u32 {
    unsafe {
        if is_drift_race != 0 {
            // In a drift race, view 4 is the drift camera; save player's prior view
            if !G_CAMERA_KNOWN {
                G_SAVED_CAMERA_VIEW = if current_view <= 3 { current_view } else { 0 };
                G_CAMERA_KNOWN = true;
            }
            4
        } else {
            // Exiting to normal race or free roam: restore prior view if reset to 3
            if G_CAMERA_KNOWN && current_view == 3 {
                let restored = G_SAVED_CAMERA_VIEW;
                G_CAMERA_KNOWN = false;
                restored
            } else {
                if current_view <= 3 {
                    G_SAVED_CAMERA_VIEW = current_view;
                    G_CAMERA_KNOWN = true;
                }
                current_view
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 5. Gearbox Straight-Cut Transmission Whine Synthesis Model
// ---------------------------------------------------------------------------

/// Computes pitch and dual-layer volumes for straight-cut transmission gear whine
/// based on vehicle speed, gear, and engine audio volumes.
#[no_mangle]
pub unsafe extern "C" fn nfs_visual_compute_transmission_whine(
    speed_kmh: f32,
    gear: i32,
    master_vol: f32,
    engine_vol: f32,
    out_pitch: *mut f32,
    out_vol1: *mut f32,
    out_vol2: *mut f32,
) {
    if out_pitch.is_null() || out_vol1.is_null() || out_vol2.is_null() {
        return;
    }

    let level = (master_vol * engine_vol).clamp(0.0, 1.0);

    if speed_kmh < 1.0 {
        *out_pitch = 0.3;
        *out_vol1 = 0.0;
        *out_vol2 = 0.0;
        return;
    }

    // Dynamic pitch curve: base 0.3 + linear rise with speed + square root surge
    let sqrt_surge = ((speed_kmh / 70.0).sqrt() * 0.5).min(0.6);
    let gear_bias = if gear > 1 { ((gear - 1) as f32) * 0.02 } else { 0.0 };
    let pitch = (0.3 + (0.7 * speed_kmh / 330.0) + sqrt_surge + gear_bias).clamp(0.2, 2.5);

    *out_pitch = pitch;

    if gear == 0 {
        // Reverse gear
        *out_vol1 = 0.05 * level;
        *out_vol2 = 0.0;
    } else {
        // Forward gears: dual layered whine
        *out_vol1 = 0.030 * level;
        *out_vol2 = (0.015 * (speed_kmh / 140.0) * level).clamp(0.0, 0.035 * level);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_simd_stream_copy() {
        let src = [0x5Au8; 128];
        let mut dst = [0u8; 128];
        unsafe {
            nfs_simd_stream_copy(dst.as_mut_ptr(), src.as_ptr(), 128);
        }
        assert_eq!(src, dst);
    }

    #[test]
    fn test_transmission_whine_pitch() {
        let mut p = 0.0f32;
        let mut v1 = 0.0f32;
        let mut v2 = 0.0f32;
        unsafe {
            nfs_visual_compute_transmission_whine(100.0, 3, 1.0, 1.0, &mut p, &mut v1, &mut v2);
        }
        assert!(p > 0.5 && p < 2.0);
        assert!(v1 > 0.0);
    }
}
