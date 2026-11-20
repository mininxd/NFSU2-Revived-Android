//! Gamepad DirectInput Bridge, 2D Radial Deadzones, and Analog Trigger Shaping for NFSU2 Android.
//!
//! Provides true circular/radial deadzone clamping (preventing diagonal lock issues),
//! progressive S-curve steering response, and direct guest gamepad state translation.

use std::sync::atomic::{AtomicI32, Ordering};

pub struct DInputBridgeEngine {
    left_x: AtomicI32,
    left_y: AtomicI32,
    right_x: AtomicI32,
    right_y: AtomicI32,
    gas: AtomicI32,
    brake: AtomicI32,
}

impl DInputBridgeEngine {
    pub const fn new() -> Self {
        Self {
            left_x: AtomicI32::new(0),
            left_y: AtomicI32::new(0),
            right_x: AtomicI32::new(0),
            right_y: AtomicI32::new(0),
            gas: AtomicI32::new(0),
            brake: AtomicI32::new(0),
        }
    }

    /// Processes 2D thumbstick input with circular/radial deadzone and non-linear S-curve.
    /// Returns `(shaped_x, shaped_y)` in range `[-1.0, 1.0]`.
    pub fn process_stick(&self, x: f32, y: f32, deadzone: f32, curve_weight: f32) -> (f32, f32) {
        let mag = (x * x + y * y).sqrt();
        let dz = deadzone.clamp(0.0, 0.45);

        if mag <= dz || mag < 1e-5 {
            return (0.0, 0.0);
        }

        let clamped_mag = mag.min(1.0);
        let scaled_mag = (clamped_mag - dz) / (1.0 - dz);
        let w = curve_weight.clamp(0.0, 1.0);
        let curved_mag = (1.0 - w) * scaled_mag + w * scaled_mag * scaled_mag * scaled_mag;

        let scale = curved_mag / mag;
        (x * scale, y * scale)
    }

    /// Processes 1D trigger input (accelerator/brake) with progressive response.
    pub fn process_trigger(&self, raw: f32, deadzone: f32) -> f32 {
        let clamped = raw.clamp(0.0, 1.0);
        let dz = deadzone.clamp(0.0, 0.30);
        if clamped <= dz {
            return 0.0;
        }
        let scaled = (clamped - dz) / (1.0 - dz);
        // Progressive pedal curve: 40% linear + 60% quadratic for smooth feathering
        0.40 * scaled + 0.60 * scaled * scaled
    }

    pub fn set_left_stick(&self, x: f32, y: f32) {
        self.left_x.store((x * 10000.0) as i32, Ordering::Release);
        self.left_y.store((y * 10000.0) as i32, Ordering::Release);
    }

    pub fn set_pedals(&self, gas: f32, brake: f32) {
        self.gas.store((gas * 10000.0) as i32, Ordering::Release);
        self.brake.store((brake * 10000.0) as i32, Ordering::Release);
    }
}

pub static GLOBAL_DINPUT_BRIDGE: DInputBridgeEngine = DInputBridgeEngine::new();

// ---- C ABI Exports ---------------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_dinput_radial_x(x: f32, y: f32, deadzone: f32, curve_weight: f32) -> f32 {
    GLOBAL_DINPUT_BRIDGE.process_stick(x, y, deadzone, curve_weight).0
}

#[no_mangle]
pub extern "C" fn nfs_dinput_radial_y(x: f32, y: f32, deadzone: f32, curve_weight: f32) -> f32 {
    GLOBAL_DINPUT_BRIDGE.process_stick(x, y, deadzone, curve_weight).1
}

#[no_mangle]
pub extern "C" fn nfs_dinput_trigger(raw: f32, deadzone: f32) -> f32 {
    GLOBAL_DINPUT_BRIDGE.process_trigger(raw, deadzone)
}
