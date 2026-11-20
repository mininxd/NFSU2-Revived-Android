//! High-precision sensor fusion and non-linear S-curve tilt steering for NFSU2 Android.
//!
//! Replaces Java-side sensor polling and binary thresholding with continuous
//! dual-rate low-pass filtering, automatic neutral calibration, circular deadzone,
//! and progressive cubic S-curve steering.

use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::Mutex;

pub struct TiltFusionEngine {
    neutral: AtomicI32,      // Fixed point (val * 10000.0)
    filtered: AtomicI32,     // Fixed point (val * 10000.0)
    needs_calibrate: AtomicBool,
    last_turn: AtomicI32,    // Fixed point
    lock: Mutex<()>,
}

impl TiltFusionEngine {
    pub const fn new() -> Self {
        Self {
            neutral: AtomicI32::new(0),
            filtered: AtomicI32::new(0),
            needs_calibrate: AtomicBool::new(true),
            last_turn: AtomicI32::new(0),
            lock: Mutex::new(()),
        }
    }

    /// Triggers neutral recalibration on next sensor event (e.g. when game resumes or player hits reset).
    pub fn request_calibration(&self) {
        self.needs_calibrate.store(true, Ordering::Release);
    }

    /// Resets filter state.
    pub fn reset(&self) {
        self.neutral.store(0, Ordering::Release);
        self.filtered.store(0, Ordering::Release);
        self.needs_calibrate.store(true, Ordering::Release);
        self.last_turn.store(0, Ordering::Release);
    }

    /// Processes raw horizontal tilt reading (typically `-event.values[1]` in landscape).
    ///
    /// Applies:
    /// 1. Auto-calibration of neutral holding position.
    /// 2. Anti-jitter low-pass filter: `filtered += 0.22 * (raw - filtered)`.
    /// 3. Deviation computation scaled by user sensitivity and inversion.
    /// 4. Deadzone clamping.
    /// 5. Non-linear S-curve: `35% linear + 65% cubic` for micro-corrections at high speeds
    ///    without sacrificing full-lock turning response.
    pub fn process_tilt(
        &self,
        raw_horizontal: f32,
        sensitivity: f32,
        invert: bool,
        deadzone: f32,
    ) -> f32 {
        let _guard = match self.lock.lock() {
            Ok(g) => g,
            Err(_) => return 0.0,
        };

        let mut current_filtered = self.filtered.load(Ordering::Acquire) as f32 / 10000.0;
        let mut current_neutral = self.neutral.load(Ordering::Acquire) as f32 / 10000.0;

        if self.needs_calibrate.swap(false, Ordering::AcqRel) {
            current_neutral = raw_horizontal;
            current_filtered = raw_horizontal;
            self.neutral.store((current_neutral * 10000.0) as i32, Ordering::Release);
        }

        // Low-pass filter (smoothing rate 0.22 per sensor tick)
        current_filtered += 0.22 * (raw_horizontal - current_filtered);
        self.filtered.store((current_filtered * 10000.0) as i32, Ordering::Release);

        let sens = sensitivity.clamp(0.2, 5.0);
        let dir = if invert { -1.0 } else { 1.0 };
        let deviation = (current_filtered - current_neutral) * sens * dir;

        // Apply progressive S-curve with deadzone
        let turn = apply_scurve(deviation, deadzone, 0.65);
        self.last_turn.store((turn * 10000.0) as i32, Ordering::Release);
        turn
    }

    /// Gets the most recent calculated turn value in range `[-1.0, 1.0]`.
    pub fn get_last_turn(&self) -> f32 {
        self.last_turn.load(Ordering::Acquire) as f32 / 10000.0
    }
}

/// Applies a progressive non-linear S-curve with deadzone clamping.
///
/// `curve_weight`: 0.0 = pure linear, 1.0 = pure cubic. Default 0.65 gives optimal stability.
#[inline]
pub fn apply_scurve(val: f32, deadzone: f32, curve_weight: f32) -> f32 {
    let clamped = val.clamp(-1.0, 1.0);
    let abs_val = clamped.abs();
    let dz = deadzone.clamp(0.0, 0.40);

    if abs_val <= dz {
        return 0.0;
    }

    // Rescale range [dz, 1.0] -> [0.0, 1.0]
    let scaled = (abs_val - dz) / (1.0 - dz);
    let w = curve_weight.clamp(0.0, 1.0);
    let curved = (1.0 - w) * scaled + w * scaled * scaled * scaled;

    if clamped < 0.0 { -curved } else { curved }
}

pub static GLOBAL_TILT_FUSION: TiltFusionEngine = TiltFusionEngine::new();

// ---- C ABI Exports ---------------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_tilt_process(raw_horizontal: f32, sensitivity: f32, invert: i32, deadzone: f32) -> f32 {
    GLOBAL_TILT_FUSION.process_tilt(raw_horizontal, sensitivity, invert != 0, deadzone)
}

#[no_mangle]
pub extern "C" fn nfs_tilt_calibrate() {
    GLOBAL_TILT_FUSION.request_calibration();
}

#[no_mangle]
pub extern "C" fn nfs_tilt_reset() {
    GLOBAL_TILT_FUSION.reset();
}

#[no_mangle]
pub extern "C" fn nfs_scurve_calculate(val: f32, deadzone: f32, curve_weight: f32) -> f32 {
    apply_scurve(val, deadzone, curve_weight)
}
