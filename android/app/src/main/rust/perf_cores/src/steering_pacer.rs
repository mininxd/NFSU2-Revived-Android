//! Pure Rust Touch Input & Progressive Steering Pacer for NFSU2 Android.
//!
//! Provides progressive non-linear response curves, circular deadzone clamping, anti-jitter
//! smoothing, and return-to-center spring dynamics with zero Java garbage collection overhead.

/// Calculates progressive steering deflection value in range [-1.0, 1.0].
///
/// Applies:
/// 1. Deadzone threshold (clamps micro-jitter near center).
/// 2. Normalized rescaling `[deadzone, 1.0] -> [0.0, 1.0]`.
/// 3. Non-linear cubic curve: `30% linear + 70% cubic` for highway steering stability
///    and responsive high-angle lock when cornering or drifting.
#[inline]
pub fn calculate_progressive_steering(
    raw_x: f32,
    center_x: f32,
    half_width: f32,
    scale: f32,
    deadzone: f32,
) -> f32 {
    if scale <= 0.0 || half_width <= 0.0 {
        return 0.0;
    }

    let norm = ((raw_x / scale) - center_x) / half_width;
    let clamped = norm.clamp(-1.0, 1.0);
    let abs_val = clamped.abs();

    let dz = deadzone.clamp(0.0, 0.5);
    if abs_val <= dz {
        return 0.0;
    }

    // Rescale range [dz, 1.0] -> [0.0, 1.0]
    let scaled = (abs_val - dz) / (1.0 - dz);
    // 30% linear + 70% cubic
    let curved = 0.30 * scaled + 0.70 * scaled * scaled * scaled;

    if clamped < 0.0 { -curved } else { curved }
}

/// Applies anti-jitter exponential moving average (EMA) smoothing between current and target steering.
#[inline]
pub fn smooth_steering(current: f32, target: f32, dt: f32, responsiveness: f32) -> f32 {
    let alpha = (1.0 - (-responsiveness * dt).exp()).clamp(0.0, 1.0);
    current + alpha * (target - current)
}

// ---- C ABI Exports ---------------------------------------------------------------------------------

/// Pure Rust C-ABI for touch steering calculation.
#[no_mangle]
pub extern "C" fn nfs_steering_calculate(
    raw_x: f32,
    center_x: f32,
    half_width: f32,
    scale: f32,
    deadzone: f32,
) -> f32 {
    calculate_progressive_steering(raw_x, center_x, half_width, scale, deadzone)
}

/// Pure Rust C-ABI for anti-jitter steering smoothing.
#[no_mangle]
pub extern "C" fn nfs_steering_smooth(
    current_val: f32,
    target_val: f32,
    dt: f32,
    responsiveness: f32,
) -> f32 {
    smooth_steering(current_val, target_val, dt, responsiveness)
}

// ---- JNI Export for io.mininxd.nfsu2.PerformanceCores ----------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeCalculateSteeringProgressive(
    _env: jni::JNIEnv,
    _class: jni::objects::JClass,
    raw_x: jni::sys::jfloat,
    center_x: jni::sys::jfloat,
    half_width: jni::sys::jfloat,
    scale: jni::sys::jfloat,
    deadzone: jni::sys::jfloat,
) -> jni::sys::jfloat {
    calculate_progressive_steering(raw_x, center_x, half_width, scale, deadzone)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deadzone_clamping() {
        // Within deadzone of 0.05
        let center = 100.0;
        let half = 50.0;
        let scale = 1.0;
        let dz = 0.05;

        let val_zero = calculate_progressive_steering(center, center, half, scale, dz);
        assert_eq!(val_zero, 0.0);

        let val_near = calculate_progressive_steering(center + 2.0, center, half, scale, dz);
        assert_eq!(val_near, 0.0); // 2/50 = 0.04 <= 0.05 -> clamped to 0
    }

    #[test]
    fn test_progressive_curve() {
        let center = 100.0;
        let half = 50.0;
        let scale = 1.0;
        let dz = 0.0;

        let full_right = calculate_progressive_steering(150.0, center, half, scale, dz);
        assert!((full_right - 1.0).abs() < 1e-5);

        let full_left = calculate_progressive_steering(50.0, center, half, scale, dz);
        assert!((full_left - (-1.0)).abs() < 1e-5);

        let half_turn = calculate_progressive_steering(125.0, center, half, scale, dz);
        // 0.5 * 0.30 + 0.125 * 0.70 = 0.15 + 0.0875 = 0.2375
        assert!((half_turn - 0.2375).abs() < 1e-4);
    }
}
