//! DirectInput 8 (dinput8.dll) Force Feedback & Haptics Engine in Rust for NFSU2 Android.
//!
//! Provides force feedback effect management, periodic/constant motor calculations,
//! actuator state machines, and vibration command dispatching for gamepads and steering wheels.

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

// DirectInput 8 Return Codes
pub const DI_OK: u32 = 0x00000000;
pub const DIERR_INVALIDPARAM: u32 = 0x80070057;
pub const DIERR_NOTINITIALIZED: u32 = 0x80070015;
pub const DIERR_DEVICEFULL: u32 = 0x80040201;
pub const DIERR_NOTDOWNLOADED: u32 = 0x80040203;
pub const DIERR_EFFECTPLAYING: u32 = 0x80040208;
pub const DIERR_UNSUPPORTED: u32 = 0x80004001;

// DirectInput Force Feedback Commands (SendForceFeedbackCommand)
pub const DISFFC_RESET: u32 = 1;
pub const DISFFC_STOPALL: u32 = 2;
pub const DISFFC_PAUSE: u32 = 4;
pub const DISFFC_CONTINUE: u32 = 8;
pub const DISFFC_SETACTUATORSON: u32 = 16;
pub const DISFFC_SETACTUATORSOFF: u32 = 32;

// DirectInput Force Feedback State Flags (GetForceFeedbackState)
pub const DIGFFS_EMPTY: u32 = 0x00000001;
pub const DIGFFS_STOPPED: u32 = 0x00000002;
pub const DIGFFS_PAUSED: u32 = 0x00000004;
pub const DIGFFS_ACTUATORSON: u32 = 0x00000010;
pub const DIGFFS_ACTUATORSOFF: u32 = 0x00000020;
pub const DIGFFS_POWERON: u32 = 0x00000040;

// DirectInput Effect Status Flags (GetEffectStatus)
pub const DIEGES_PLAYING: u32 = 0x00000001;
pub const DIEGES_EMULATED: u32 = 0x00000002;

// Effect Start Flags
pub const DIES_SOLO: u32 = 0x00000001;
pub const DIES_NODOWNLOAD: u32 = 0x80000000;

// DirectInput Effect Standard GUID Data1 Identifiers
pub const GUID_CONSTANT_FORCE: u32 = 0x13548632;
pub const GUID_RAMP_FORCE: u32 = 0x13548633;
pub const GUID_SQUARE: u32 = 0x13548634;
pub const GUID_SINE: u32 = 0x13548635;
pub const GUID_TRIANGLE: u32 = 0x13548636;
pub const GUID_SAWTOOTH_UP: u32 = 0x13548637;
pub const GUID_SAWTOOTH_DOWN: u32 = 0x13548638;
pub const GUID_SPRING: u32 = 0x13548639;
pub const GUID_DAMPER: u32 = 0x1354863A;
pub const GUID_INERTIA: u32 = 0x1354863B;
pub const GUID_FRICTION: u32 = 0x1354863C;

const MAX_EFFECTS: usize = 32;
const INFINITE_DURATION: u32 = 0xFFFFFFFF;

/// Internal representation of a DirectInput force feedback effect.
#[derive(Clone, Debug)]
pub struct DirectInputEffect {
    pub id: u32,
    pub effect_guid: u32,
    pub duration_us: u32,
    pub gain: u32,
    pub magnitude: i32,
    pub period_us: u32,
    pub playing: bool,
    pub iterations: u32,
    pub start_time_ms: u64,
}

impl DirectInputEffect {
    pub fn new(id: u32, effect_guid: u32, duration_us: u32, gain: u32, magnitude: i32, period_us: u32) -> Self {
        Self {
            id,
            effect_guid,
            duration_us,
            gain: if gain == 0 { 10000 } else { gain.min(10000) },
            magnitude: magnitude.clamp(-10000, 10000),
            period_us: if period_us == 0 { 100_000 } else { period_us },
            playing: false,
            iterations: 1,
            start_time_ms: 0,
        }
    }

    pub fn start(&mut self, iterations: u32, current_time_ms: u64) {
        self.playing = true;
        self.iterations = if iterations == 0 { 1 } else { iterations };
        self.start_time_ms = current_time_ms;
    }

    pub fn stop(&mut self) {
        self.playing = false;
        self.start_time_ms = 0;
    }

    pub fn is_active(&self, current_time_ms: u64) -> bool {
        if !self.playing {
            return false;
        }
        if self.duration_us == INFINITE_DURATION {
            return true;
        }
        let duration_ms = (self.duration_us as u64) / 1000;
        let total_duration_ms = duration_ms.saturating_mul(self.iterations as u64);
        current_time_ms < self.start_time_ms.saturating_add(total_duration_ms)
    }

    pub fn current_intensity(&self, current_time_ms: u64) -> f32 {
        if !self.is_active(current_time_ms) {
            return 0.0;
        }
        let mag = (self.magnitude.abs() as f32) / 10000.0;
        let g = (self.gain as f32) / 10000.0;
        (mag * g).clamp(0.0, 1.0)
    }
}

/// Global Force Feedback State Machine for DirectInput Gamepad / Wheel.
pub struct DirectInputFeedbackEngine {
    pub actuators_on: bool,
    pub paused: bool,
    pub effects: HashMap<u32, DirectInputEffect>,
    pub next_effect_id: u32,
    pub left_motor: f32,   // Low-frequency rumble (0.0 .. 1.0)
    pub right_motor: f32,  // High-frequency rumble (0.0 .. 1.0)
}

impl DirectInputFeedbackEngine {
    pub fn new() -> Self {
        Self {
            actuators_on: true,
            paused: false,
            effects: HashMap::new(),
            next_effect_id: 1,
            left_motor: 0.0,
            right_motor: 0.0,
        }
    }

    fn current_time_ms() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as u64)
            .unwrap_or(0)
    }

    pub fn create_effect(
        &mut self,
        guid_data1: u32,
        duration_us: u32,
        gain: u32,
        magnitude: i32,
        period_us: u32,
    ) -> Result<u32, u32> {
        if self.effects.len() >= MAX_EFFECTS {
            return Err(DIERR_DEVICEFULL);
        }
        let id = self.next_effect_id;
        self.next_effect_id = self.next_effect_id.wrapping_add(1).max(1);

        let effect = DirectInputEffect::new(id, guid_data1, duration_us, gain, magnitude, period_us);
        self.effects.insert(id, effect);
        self.update_motors();
        Ok(id)
    }

    pub fn destroy_effect(&mut self, effect_id: u32) -> u32 {
        if self.effects.remove(&effect_id).is_some() {
            self.update_motors();
            DI_OK
        } else {
            DIERR_INVALIDPARAM
        }
    }

    pub fn start_effect(&mut self, effect_id: u32, iterations: u32, _flags: u32) -> u32 {
        let now = Self::current_time_ms();
        if let Some(effect) = self.effects.get_mut(&effect_id) {
            effect.start(iterations, now);
            self.update_motors();
            DI_OK
        } else {
            DIERR_INVALIDPARAM
        }
    }

    pub fn stop_effect(&mut self, effect_id: u32) -> u32 {
        if let Some(effect) = self.effects.get_mut(&effect_id) {
            effect.stop();
            self.update_motors();
            DI_OK
        } else {
            DIERR_INVALIDPARAM
        }
    }

    pub fn get_effect_status(&mut self, effect_id: u32) -> Result<u32, u32> {
        let now = Self::current_time_ms();
        if let Some(effect) = self.effects.get_mut(&effect_id) {
            if effect.is_active(now) {
                Ok(DIEGES_PLAYING | DIEGES_EMULATED)
            } else {
                effect.playing = false;
                Ok(0)
            }
        } else {
            Err(DIERR_INVALIDPARAM)
        }
    }

    pub fn set_effect_params(
        &mut self,
        effect_id: u32,
        duration_us: u32,
        gain: u32,
        magnitude: i32,
        period_us: u32,
    ) -> u32 {
        if let Some(effect) = self.effects.get_mut(&effect_id) {
            if duration_us > 0 {
                effect.duration_us = duration_us;
            }
            if gain > 0 {
                effect.gain = gain.min(10000);
            }
            effect.magnitude = magnitude.clamp(-10000, 10000);
            if period_us > 0 {
                effect.period_us = period_us;
            }
            self.update_motors();
            DI_OK
        } else {
            DIERR_INVALIDPARAM
        }
    }

    pub fn send_command(&mut self, command: u32) -> u32 {
        match command {
            DISFFC_RESET => {
                self.effects.clear();
                self.actuators_on = true;
                self.paused = false;
                self.left_motor = 0.0;
                self.right_motor = 0.0;
                DI_OK
            }
            DISFFC_STOPALL => {
                for effect in self.effects.values_mut() {
                    effect.stop();
                }
                self.update_motors();
                DI_OK
            }
            DISFFC_PAUSE => {
                self.paused = true;
                self.update_motors();
                DI_OK
            }
            DISFFC_CONTINUE => {
                self.paused = false;
                self.update_motors();
                DI_OK
            }
            DISFFC_SETACTUATORSON => {
                self.actuators_on = true;
                self.update_motors();
                DI_OK
            }
            DISFFC_SETACTUATORSOFF => {
                self.actuators_on = false;
                self.update_motors();
                DI_OK
            }
            _ => DIERR_INVALIDPARAM,
        }
    }

    pub fn get_state(&self) -> u32 {
        let mut state = DIGFFS_POWERON;
        if self.effects.is_empty() {
            state |= DIGFFS_EMPTY;
        }
        if self.paused {
            state |= DIGFFS_PAUSED;
        }
        if self.actuators_on {
            state |= DIGFFS_ACTUATORSON;
        } else {
            state |= DIGFFS_ACTUATORSOFF;
        }
        let now = Self::current_time_ms();
        let any_playing = self.effects.values().any(|e| e.is_active(now));
        if !any_playing {
            state |= DIGFFS_STOPPED;
        }
        state
    }

    pub fn update_motors(&mut self) {
        if !self.actuators_on || self.paused {
            self.left_motor = 0.0;
            self.right_motor = 0.0;
            return;
        }

        let now = Self::current_time_ms();
        let mut left_sum = 0.0f32;
        let mut right_sum = 0.0f32;

        for effect in self.effects.values_mut() {
            if !effect.is_active(now) {
                effect.playing = false;
                continue;
            }

            let intensity = effect.current_intensity(now);
            match effect.effect_guid {
                // Constant force, springs, friction, dampers -> primary low frequency impact
                GUID_CONSTANT_FORCE | GUID_SPRING | GUID_DAMPER | GUID_FRICTION | GUID_INERTIA => {
                    left_sum = left_sum.max(intensity);
                    right_sum = right_sum.max(intensity * 0.4);
                }
                // Periodic effects (sine, square, triangle, sawtooth) -> high frequency rumble
                GUID_SINE | GUID_SQUARE | GUID_TRIANGLE | GUID_SAWTOOTH_UP | GUID_SAWTOOTH_DOWN => {
                    right_sum = right_sum.max(intensity);
                    left_sum = left_sum.max(intensity * 0.5);
                }
                // Ramp / generic fallback -> equal balance
                _ => {
                    left_sum = left_sum.max(intensity * 0.8);
                    right_sum = right_sum.max(intensity * 0.8);
                }
            }
        }

        self.left_motor = left_sum.clamp(0.0, 1.0);
        self.right_motor = right_sum.clamp(0.0, 1.0);
    }
}

static FEEDBACK_ENGINE: Mutex<Option<DirectInputFeedbackEngine>> = Mutex::new(None);

fn with_engine<F, R>(f: F) -> R
where
    F: FnOnce(&mut DirectInputFeedbackEngine) -> R,
{
    let mut lock = FEEDBACK_ENGINE.lock().unwrap();
    if lock.is_none() {
        *lock = Some(DirectInputFeedbackEngine::new());
    }
    f(lock.as_mut().unwrap())
}

// C-ABI Exports for DirectInput 8 Force Feedback

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_create_effect(
    guid_data1: u32,
    duration_us: u32,
    gain: u32,
    magnitude: i32,
    period_us: u32,
    out_effect_id: *mut u32,
) -> u32 {
    if out_effect_id.is_null() {
        return DIERR_INVALIDPARAM;
    }
    with_engine(|engine| match engine.create_effect(guid_data1, duration_us, gain, magnitude, period_us) {
        Ok(id) => {
            *out_effect_id = id;
            DI_OK
        }
        Err(err) => err,
    })
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_destroy_effect(effect_id: u32) -> u32 {
    with_engine(|engine| engine.destroy_effect(effect_id))
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_start_effect(effect_id: u32, iterations: u32, flags: u32) -> u32 {
    with_engine(|engine| engine.start_effect(effect_id, iterations, flags))
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_stop_effect(effect_id: u32) -> u32 {
    with_engine(|engine| engine.stop_effect(effect_id))
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_get_effect_status(effect_id: u32, out_status: *mut u32) -> u32 {
    if out_status.is_null() {
        return DIERR_INVALIDPARAM;
    }
    with_engine(|engine| match engine.get_effect_status(effect_id) {
        Ok(status) => {
            *out_status = status;
            DI_OK
        }
        Err(err) => err,
    })
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_set_effect_params(
    effect_id: u32,
    duration_us: u32,
    gain: u32,
    magnitude: i32,
    period_us: u32,
) -> u32 {
    with_engine(|engine| engine.set_effect_params(effect_id, duration_us, gain, magnitude, period_us))
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_send_ff_command(command: u32) -> u32 {
    with_engine(|engine| engine.send_command(command))
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_get_ff_state(out_state: *mut u32) -> u32 {
    if out_state.is_null() {
        return DIERR_INVALIDPARAM;
    }
    with_engine(|engine| {
        *out_state = engine.get_state();
        DI_OK
    })
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_get_motor_speeds(out_left: *mut f32, out_right: *mut f32) -> u32 {
    with_engine(|engine| {
        engine.update_motors();
        if !out_left.is_null() {
            *out_left = engine.left_motor;
        }
        if !out_right.is_null() {
            *out_right = engine.right_motor;
        }
        DI_OK
    })
}

#[no_mangle]
pub unsafe extern "C" fn rust_dinput_reset() {
    with_engine(|engine| {
        let _ = engine.send_command(DISFFC_RESET);
    });
}
