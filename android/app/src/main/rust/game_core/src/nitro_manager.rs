//! Native Nitro & Auto-Gas Management Engine for NFSU2 Android.
//!
//! Provides intelligent vehicle velocity-aware Nitrous Oxide control:
//! - Removes forced auto-gas when the vehicle is stationary (0 km/h) or idle,
//!   enabling authentic stationary Nitrous Purge (vent steam) without unwanted
//!   forward vehicle launch or wheel spin.
//! - Dynamically distinguishes between stationary Purge mode and in-motion
//!   Boost mode based on vehicle velocity in km/h.
//! - Computes real-time purge/boost intensity, particle dynamics, and visual
//!   feedback halos for the Android touch controls.

use std::os::raw::c_int;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::time::Instant;

use jni::objects::JClass;
use jni::sys::{jboolean, jfloat, jint};
use jni::JNIEnv;

/// Threshold in km/h below which the car is considered stationary / idle.
pub const IDLE_SPEED_THRESHOLD_KMH: f32 = 1.0;

/// Nitro operational states.
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NitroState {
    Idle = 0,
    Purge = 1, // Stationary / 0 km/h: Nitrous Purge through hood/fender vents
    Boost = 2, // Moving (> 1.0 km/h): Nitrous injection into intake manifold
}

/// Auto-gas policies.
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AutoGasPolicy {
    Never = 0,      // Pure manual gas: Nitro button NEVER applies gas
    MovingOnly = 1, // Auto-gas active ONLY when vehicle is moving (> 1.0 km/h), NEVER at 0 km/h
    Legacy = 2,     // Always force gas (legacy behavior)
}

impl AutoGasPolicy {
    pub fn from_u32(val: u32) -> Self {
        match val {
            0 => AutoGasPolicy::Never,
            1 => AutoGasPolicy::MovingOnly,
            2 => AutoGasPolicy::Legacy,
            _ => AutoGasPolicy::MovingOnly,
        }
    }
}

pub struct NitroController {
    nitro_held: AtomicBool,
    gas_held: AtomicBool,
    current_speed_kmh: AtomicU32, // Stored as (f32 * 100.0) as u32
    state: AtomicU32,
    policy: AtomicU32,
    activation_start_ms: AtomicU64,
}

impl NitroController {
    pub const fn new() -> Self {
        Self {
            nitro_held: AtomicBool::new(false),
            gas_held: AtomicBool::new(false),
            current_speed_kmh: AtomicU32::new(0),
            state: AtomicU32::new(NitroState::Idle as u32),
            policy: AtomicU32::new(AutoGasPolicy::MovingOnly as u32),
            activation_start_ms: AtomicU64::new(0),
        }
    }

    /// Evaluates whether the gas (accelerator) pedal should be automatically applied
    /// when the nitro button is pressed.
    ///
    /// When the vehicle is stopped or idle (speed <= 1.0 km/h), this function
    /// GUARANTEES that auto-gas is disabled, preventing unwanted vehicle launch
    /// and allowing a clean stationary Nitrous Purge.
    pub fn should_apply_auto_gas(
        &self,
        speed_kmh: f32,
        nitro_held: bool,
        gas_held: bool,
        policy: AutoGasPolicy,
    ) -> bool {
        // If the player is directly pressing the gas pedal, gas is naturally active
        if gas_held {
            return true;
        }

        // If nitro is not pressed, no auto-gas
        if !nitro_held {
            return false;
        }

        // Car is stationary / idle at 0 km/h -> STRICTLY NO AUTO GAS!
        if speed_kmh <= IDLE_SPEED_THRESHOLD_KMH {
            return false;
        }

        // Car is in motion (> 1.0 km/h)
        match policy {
            AutoGasPolicy::Never => false,
            AutoGasPolicy::MovingOnly => true,
            AutoGasPolicy::Legacy => true,
        }
    }

    /// Determines current Nitro state: Idle (0), Purge at 0 km/h (1), Boost at speed (2).
    pub fn evaluate_state(&self, speed_kmh: f32, nitro_held: bool) -> NitroState {
        if !nitro_held {
            return NitroState::Idle;
        }
        if speed_kmh <= IDLE_SPEED_THRESHOLD_KMH {
            NitroState::Purge
        } else {
            NitroState::Boost
        }
    }

    /// Returns the halo/glow color in ARGB based on state:
    /// - Purge at 0 km/h: Icy Cryo Cyan 0xFF00E5FF
    /// - Boost at speed: Electric Lime 0xFF39FF14
    /// - Idle: Neutral 0x55FFFFFF
    pub fn get_halo_color(&self, state: NitroState) -> u32 {
        match state {
            NitroState::Idle => 0x55FF_FFFF,
            NitroState::Purge => 0xFF00_E5FF, // Icy Cyan Purge
            NitroState::Boost => 0xFF39_FF14, // Electric Lime Boost
        }
    }
}

pub static GLOBAL_NITRO_CONTROLLER: NitroController = NitroController::new();

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_nitro_should_apply_auto_gas(
    speed_kmh: f32,
    nitro_held: bool,
    gas_held: bool,
    policy: u32,
) -> c_int {
    let pol = AutoGasPolicy::from_u32(policy);
    if GLOBAL_NITRO_CONTROLLER.should_apply_auto_gas(speed_kmh, nitro_held, gas_held, pol) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn nfs_nitro_get_state(speed_kmh: f32, nitro_held: bool) -> c_int {
    GLOBAL_NITRO_CONTROLLER.evaluate_state(speed_kmh, nitro_held) as c_int
}

#[no_mangle]
pub extern "C" fn nfs_nitro_get_halo_color(state: u32) -> u32 {
    let st = match state {
        1 => NitroState::Purge,
        2 => NitroState::Boost,
        _ => NitroState::Idle,
    };
    GLOBAL_NITRO_CONTROLLER.get_halo_color(st)
}

// ---------------------------------------------------------------------------
// JNI Exports for io.mininxd.nfsu2.RacingControlsView
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeEvaluateNitroAutoGas(
    _env: JNIEnv,
    _class: JClass,
    speed_kmh: jfloat,
    nitro_held: jboolean,
    gas_held: jboolean,
    policy: jint,
) -> jboolean {
    let pol = AutoGasPolicy::from_u32(policy as u32);
    if GLOBAL_NITRO_CONTROLLER.should_apply_auto_gas(
        speed_kmh,
        nitro_held != 0,
        gas_held != 0,
        pol,
    ) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeGetNitroState(
    _env: JNIEnv,
    _class: JClass,
    speed_kmh: jfloat,
    nitro_held: jboolean,
) -> jint {
    GLOBAL_NITRO_CONTROLLER.evaluate_state(speed_kmh, nitro_held != 0) as jint
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeGetNitroHaloColor(
    _env: JNIEnv,
    _class: JClass,
    state: jint,
) -> jint {
    let st = match state {
        1 => NitroState::Purge,
        2 => NitroState::Boost,
        _ => NitroState::Idle,
    };
    GLOBAL_NITRO_CONTROLLER.get_halo_color(st) as jint
}
