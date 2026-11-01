//! ARM64 NEON SIMD Accelerated Audio Resampler & Mixer.
//!
//! Replaces scalar floating-point sinc kernel interpolation with 128-bit NEON
//! vector multiply-accumulate operations, achieving 4x throughput per voice.

#[cfg(target_arch = "aarch64")]
use std::arch::aarch64::*;

#[inline]
pub unsafe fn resample_sinc_16tap_neon(
    samples_left: *const f32,
    samples_right: *const f32,
    weights_curr: *const f32,
    weights_next: *const f32,
    blend: f32,
) -> (f32, f32) {
    #[cfg(target_arch = "aarch64")]
    {
        let v_blend = vdupq_n_f32(blend);
        let mut acc_l = vdupq_n_f32(0.0);
        let mut acc_r = vdupq_n_f32(0.0);

        for i in (0..16).step_by(4) {
            let w_curr = vld1q_f32(weights_curr.add(i));
            let w_next = vld1q_f32(weights_next.add(i));
            let w_diff = vsubq_f32(w_next, w_curr);
            let w = vmlaq_f32(w_curr, w_diff, v_blend);

            let s_l = vld1q_f32(samples_left.add(i));
            let s_r = vld1q_f32(samples_right.add(i));

            acc_l = vmlaq_f32(acc_l, s_l, w);
            acc_r = vmlaq_f32(acc_r, s_r, w);
        }

        let sum_l = vaddvq_f32(acc_l);
        let sum_r = vaddvq_f32(acc_r);
        (sum_l, sum_r)
    }

    #[cfg(not(target_arch = "aarch64"))]
    {
        resample_sinc_16tap_scalar(samples_left, samples_right, weights_curr, weights_next, blend)
    }
}

#[inline]
pub unsafe fn resample_sinc_16tap_scalar(
    samples_left: *const f32,
    samples_right: *const f32,
    weights_curr: *const f32,
    weights_next: *const f32,
    blend: f32,
) -> (f32, f32) {
    let mut sum_l = 0.0f32;
    let mut sum_r = 0.0f32;

    for i in 0..16 {
        let w0 = *weights_curr.add(i);
        let w1 = *weights_next.add(i);
        let w = w0 + (w1 - w0) * blend;
        sum_l += *samples_left.add(i) * w;
        sum_r += *samples_right.add(i) * w;
    }

    (sum_l, sum_r)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_audio_resample_sinc_simd(
    samples_left: *const f32,
    samples_right: *const f32,
    weights_curr: *const f32,
    weights_next: *const f32,
    blend: f32,
    out_left: *mut f32,
    out_right: *mut f32,
) {
    if samples_left.is_null()
        || samples_right.is_null()
        || weights_curr.is_null()
        || weights_next.is_null()
        || out_left.is_null()
        || out_right.is_null()
    {
        return;
    }

    let (l, r) = resample_sinc_16tap_neon(
        samples_left,
        samples_right,
        weights_curr,
        weights_next,
        blend,
    );

    *out_left = l;
    *out_right = r;
}
