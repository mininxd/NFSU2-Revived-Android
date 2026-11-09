//! Real-Time Audio Mixer & Resampling Acceleration in Rust for NFSU2 Android.
//!
//! Provides batch voice sample decoding, fast-path linear stereo resampling, and
//! vectorized NEON peak/clipping statistics for AAudio real-time output.

#[cfg(target_arch = "aarch64")]
use core::arch::aarch64::*;

#[inline(always)]
unsafe fn read_pcm_sample(samples: *const u8, block_stride: usize, bits: u32, frame: usize, channel: usize) -> f32 {
    let offset = frame * block_stride + channel * (bits as usize / 8);
    if bits == 8 {
        let byte = *samples.add(offset);
        (byte as i32 - 128) as f32 * (1.0 / 128.0)
    } else {
        let low = *samples.add(offset) as u16;
        let high = *samples.add(offset + 1) as u16;
        let word = (low | (high << 8)) as i16;
        word as f32 * (1.0 / 32768.0)
    }
}

/// Renders and accumulates a voice's samples into the stereo output buffer using linear interpolation.
///
/// Returns 1 if the voice is still playing, or 0 if it has completed.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_mixer_render_linear(
    output: *mut f32,
    frames: u32,
    samples: *const u8,
    bits: u32,
    channels: u32,
    total_frames: usize,
    mut pos: f64,
    step: f64,
    loop_mode: i32,
    gain_l: f32,
    gain_r: f32,
    pos_out: *mut f64,
) -> i32 {
    if output.is_null() || samples.is_null() || total_frames == 0 || frames == 0 {
        return 0;
    }

    let block_stride = (channels * (bits / 8)) as usize;
    let mut still_playing = 1;

    for frame in 0..frames as usize {
        if pos >= total_frames as f64 {
            if loop_mode != 0 {
                pos %= total_frames as f64;
            } else {
                still_playing = 0;
                pos = 0.0;
                break;
            }
        }

        let a = pos as usize;
        let mut b = a + 1;
        let fraction = (pos - a as f64) as f32;

        if b >= total_frames {
            b = if loop_mode != 0 { 0 } else { a };
        }

        let ch0_idx = 0;
        let ch1_idx = if channels > 1 { 1 } else { 0 };

        let first_l = read_pcm_sample(samples, block_stride, bits, a, ch0_idx);
        let second_l = read_pcm_sample(samples, block_stride, bits, b, ch0_idx);
        let val_l = first_l + (second_l - first_l) * fraction;

        let first_r = read_pcm_sample(samples, block_stride, bits, a, ch1_idx);
        let second_r = read_pcm_sample(samples, block_stride, bits, b, ch1_idx);
        let val_r = first_r + (second_r - first_r) * fraction;

        let out_idx = frame * 2;
        *output.add(out_idx) += val_l * gain_l;
        *output.add(out_idx + 1) += val_r * gain_r;

        pos += step;
    }

    if !pos_out.is_null() {
        *pos_out = pos;
    }

    still_playing
}

/// Applies vector soft-clipping [-1.0, 1.0] and computes peak amplitude and clipped sample counts.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_mixer_clamp_and_stats(
    output: *mut f32,
    total_samples: usize,
    out_peak: *mut f32,
    out_clipped: *mut u64,
) {
    if output.is_null() || total_samples == 0 {
        return;
    }

    let mut peak = 0.0f32;
    let mut clipped = 0u64;

    #[cfg(target_arch = "aarch64")]
    {
        let v_neg_one = vdupq_n_f32(-1.0);
        let v_pos_one = vdupq_n_f32(1.0);
        let mut v_peak = vdupq_n_f32(0.0);

        let chunks = total_samples / 4;
        let remainder = total_samples % 4;

        for i in 0..chunks {
            let ptr = output.add(i * 4);
            let v = vld1q_f32(ptr);
            let v_abs = vabsq_f32(v);
            v_peak = vmaxq_f32(v_peak, v_abs);

            // Clamp: max(-1.0, min(1.0, v))
            let v_clamped = vmaxq_f32(v_neg_one, vminq_f32(v_pos_one, v));
            vst1q_f32(ptr, v_clamped);
        }

        let mut peak_lanes = [0.0f32; 4];
        vst1q_f32(peak_lanes.as_mut_ptr(), v_peak);
        peak = peak_lanes[0].max(peak_lanes[1]).max(peak_lanes[2]).max(peak_lanes[3]);

        for i in 0..(chunks * 4) {
            let s = *output.add(i);
            if s >= 1.0 || s <= -1.0 {
                clipped += 1;
            }
        }

        // Handle remainder
        for i in (chunks * 4)..total_samples {
            let s = *output.add(i);
            let abs_s = s.abs();
            if abs_s > peak {
                peak = abs_s;
            }
            if abs_s > 1.0 {
                clipped += 1;
                *output.add(i) = s.clamp(-1.0, 1.0);
            }
        }
    }

    #[cfg(not(target_arch = "aarch64"))]
    {
        for i in 0..total_samples {
            let s = *output.add(i);
            let abs_s = s.abs();
            if abs_s > peak {
                peak = abs_s;
            }
            if abs_s > 1.0 {
                clipped += 1;
                *output.add(i) = s.clamp(-1.0, 1.0);
            }
        }
    }

    if !out_peak.is_null() {
        *out_peak = peak;
    }
    if !out_clipped.is_null() {
        *out_clipped += clipped;
    }
}

/// Batch calculates 3D audio attenuation and stereo panning for sound emitters.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_3d_positional_batch(
    count: usize,
    emitters_xyz: *const f32, // count * 3
    listener_pos: *const f32, // 3
    listener_fwd: *const f32, // 3
    listener_rgt: *const f32, // 3
    min_dist: f32,
    max_dist: f32,
    out_gains_lr: *mut f32,   // count * 2 (gain_l, gain_r)
) {
    if emitters_xyz.is_null() || listener_pos.is_null() || listener_rgt.is_null() || out_gains_lr.is_null() || count == 0 {
        return;
    }

    let lx = *listener_pos;
    let ly = *listener_pos.add(1);
    let lz = *listener_pos.add(2);

    let rx = *listener_rgt;
    let ry = *listener_rgt.add(1);
    let rz = *listener_rgt.add(2);

    let dist_range = (max_dist - min_dist).max(0.001);

    for i in 0..count {
        let ex = *emitters_xyz.add(i * 3);
        let ey = *emitters_xyz.add(i * 3 + 1);
        let ez = *emitters_xyz.add(i * 3 + 2);

        let dx = ex - lx;
        let dy = ey - ly;
        let dz = ez - lz;

        let dist_sq = dx * dx + dy * dy + dz * dz;
        let dist = dist_sq.sqrt();

        // Linear roll-off clamped between min_dist and max_dist
        let atten = if dist <= min_dist {
            1.0
        } else if dist >= max_dist {
            0.0
        } else {
            (max_dist - dist) / dist_range
        };

        // Panning dot product with listener right vector
        let pan = if dist > 0.001 {
            let inv_d = 1.0 / dist;
            let ndx = dx * inv_d;
            let ndy = dy * inv_d;
            let ndz = dz * inv_d;
            (ndx * rx + ndy * ry + ndz * rz).clamp(-1.0, 1.0)
        } else {
            0.0
        };

        let gain_l = atten * (0.5 - 0.5 * pan);
        let gain_r = atten * (0.5 + 0.5 * pan);

        *out_gains_lr.add(i * 2) = gain_l;
        *out_gains_lr.add(i * 2 + 1) = gain_r;
    }
}
