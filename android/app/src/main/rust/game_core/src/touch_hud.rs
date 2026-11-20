//! Native Touch Controls & On-Screen Racing HUD in Safe Rust for NFSU2 Android.
//!
//! Replaces Android View hierarchy overhead (zero GC pauses at 60/90/120Hz) with
//! zero-allocation multi-touch finger tracking, DirectInput scancode generation,
//! and screen-space vertex quad synthesis for direct GLES/Vulkan rendering.

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU64, Ordering};
use std::sync::RwLock;

// ---------------------------------------------------------------------------
// DirectInput Scan Codes
// ---------------------------------------------------------------------------
pub const SCAN_ESC: i32 = 0x01;
pub const SCAN_TAB: i32 = 0x0F;
pub const SCAN_ENTER: i32 = 0x1C;
pub const SCAN_T: i32 = 0x14;
pub const SCAN_H: i32 = 0x23;
pub const SCAN_J: i32 = 0x24;
pub const SCAN_X: i32 = 0x2D;
pub const SCAN_M: i32 = 0x32;
pub const SCAN_UP: i32 = 0xC8;
pub const SCAN_DOWN: i32 = 0xD0;
pub const SCAN_LEFT: i32 = 0xCB;
pub const SCAN_RIGHT: i32 = 0xCD;

// Special Control IDs
pub const CTRL_MODE: i32 = -1;
pub const CTRL_TEXT: i32 = -2;
pub const CTRL_STICK: i32 = -3;
pub const CTRL_EDIT: i32 = -5;

// Control Shapes
pub const SHAPE_ROUND: u32 = 0;
pub const SHAPE_PEDAL: u32 = 1;
pub const SHAPE_WHEEL: u32 = 2;
pub const SHAPE_PILL: u32 = 3;
pub const SHAPE_DPAD: u32 = 4;

// Default Action Scancodes
pub const DEFAULT_GAS: i32 = 0xC8;      // Up arrow
pub const DEFAULT_BRAKE: i32 = 0xD0;    // Down arrow
pub const DEFAULT_LEFT: i32 = 0xCB;     // Left arrow
pub const DEFAULT_RIGHT: i32 = 0xCD;    // Right arrow
pub const DEFAULT_HANDBRAKE: i32 = 0x39;// Space
pub const DEFAULT_NITRO: i32 = 0x2A;    // Left Shift
pub const DEFAULT_SHIFT_UP: i32 = 0x1E;  // A
pub const DEFAULT_SHIFT_DOWN: i32 = 0x2C;// Z
pub const DEFAULT_LOOK_BACK: i32 = 0x13; // R
pub const DEFAULT_CAMERA: i32 = 0x2E;   // C
pub const DEFAULT_RESET: i32 = 0x10;    // Q

pub const MAX_FINGERS: usize = 32;

// ---------------------------------------------------------------------------
// Touch Quad Vertex Format for GLES / Vulkan Direct Drawing
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HudVertex {
    pub x: f32,
    pub y: f32,
    pub u: f32,
    pub v: f32,
    pub color_rgba: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HudQuad {
    pub v0: HudVertex,
    pub v1: HudVertex,
    pub v2: HudVertex,
    pub v3: HudVertex,
}

// ---------------------------------------------------------------------------
// Virtual Touch Control Definition
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct VirtualControl {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub shape: u32,
    pub scan: i32,
    pub held_count: u32,
    pub steering: f32,
    pub active_finger: i32,
}

impl VirtualControl {
    pub fn new(x: f32, y: f32, w: f32, h: f32, shape: u32, scan: i32) -> Self {
        Self {
            x,
            y,
            w,
            h,
            shape,
            scan,
            held_count: 0,
            steering: 0.0,
            active_finger: -1,
        }
    }

    #[inline]
    pub fn contains(&self, px: f32, py: f32) -> bool {
        px >= self.x && px <= (self.x + self.w) && py >= self.y && py <= (self.y + self.h)
    }

    #[inline]
    pub fn center(&self) -> (f32, f32) {
        (self.x + self.w * 0.5, self.y + self.h * 0.5)
    }
}

// ---------------------------------------------------------------------------
// Touch HUD Manager
// ---------------------------------------------------------------------------

pub struct TouchHudEngine {
    width: f32,
    height: f32,
    opacity: f32,
    racing_mode: bool,
    controls: Vec<VirtualControl>,
    finger_map: [Option<usize>; MAX_FINGERS], // Pointer ID -> Control index
    key_states: [bool; 256],

    // Action Key Bindings
    pub key_gas: i32,
    pub key_brake: i32,
    pub key_left: i32,
    pub key_right: i32,
    pub key_hb: i32,
    pub key_nitro: i32,
    pub key_shift_up: i32,
    pub key_shift_down: i32,
    pub key_look_back: i32,
    pub key_camera: i32,
    pub key_reset: i32,
}

impl TouchHudEngine {
    pub fn new() -> Self {
        Self {
            width: 1920.0,
            height: 1080.0,
            opacity: 0.85,
            racing_mode: true,
            controls: Vec::new(),
            finger_map: [None; MAX_FINGERS],
            key_states: [false; 256],
            key_gas: DEFAULT_GAS,
            key_brake: DEFAULT_BRAKE,
            key_left: DEFAULT_LEFT,
            key_right: DEFAULT_RIGHT,
            key_hb: DEFAULT_HANDBRAKE,
            key_nitro: DEFAULT_NITRO,
            key_shift_up: DEFAULT_SHIFT_UP,
            key_shift_down: DEFAULT_SHIFT_DOWN,
            key_look_back: DEFAULT_LOOK_BACK,
            key_camera: DEFAULT_CAMERA,
            key_reset: DEFAULT_RESET,
        }
    }

    pub fn resize(&mut self, width: f32, height: f32) {
        self.width = width.max(100.0);
        self.height = height.max(100.0);
        self.rebuild_layout();
    }

    pub fn set_racing_mode(&mut self, racing: bool) {
        self.racing_mode = racing;
        self.release_all();
        self.rebuild_layout();
    }

    pub fn set_opacity(&mut self, opacity: f32) {
        self.opacity = opacity.clamp(0.10, 1.0);
    }

    pub fn rebuild_layout(&mut self) {
        self.controls.clear();
        let w = self.width;
        let h = self.height;
        let bottom = h - 24.0;

        if self.racing_mode {
            // Action headers (top row)
            self.controls.push(VirtualControl::new(24.0, 24.0, 56.0, 56.0, SHAPE_ROUND, SCAN_ESC));
            self.controls.push(VirtualControl::new(96.0, 24.0, 56.0, 56.0, SHAPE_ROUND, self.key_camera));
            self.controls.push(VirtualControl::new(168.0, 24.0, 56.0, 56.0, SHAPE_ROUND, self.key_look_back));
            self.controls.push(VirtualControl::new(240.0, 24.0, 56.0, 56.0, SHAPE_ROUND, self.key_reset));
            self.controls.push(VirtualControl::new(312.0, 24.0, 56.0, 56.0, SHAPE_ROUND, SCAN_M));
            self.controls.push(VirtualControl::new(384.0, 24.0, 56.0, 56.0, SHAPE_ROUND, SCAN_TAB));
            self.controls.push(VirtualControl::new(456.0, 24.0, 56.0, 56.0, SHAPE_ROUND, SCAN_X));

            // Steering Left / Right (Bottom Left)
            self.controls.push(VirtualControl::new(32.0, bottom - 120.0, 110.0, 110.0, SHAPE_DPAD, self.key_left));
            self.controls.push(VirtualControl::new(154.0, bottom - 120.0, 110.0, 110.0, SHAPE_DPAD, self.key_right));

            // Shift Up / Down
            self.controls.push(VirtualControl::new(32.0, bottom - 240.0, 96.0, 56.0, SHAPE_ROUND, self.key_shift_down));
            self.controls.push(VirtualControl::new(140.0, bottom - 240.0, 96.0, 56.0, SHAPE_ROUND, self.key_shift_up));

            // Pedals & Nitro (Bottom Right)
            self.controls.push(VirtualControl::new(w - 120.0, bottom - 200.0, 100.0, 200.0, SHAPE_PEDAL, self.key_gas));
            self.controls.push(VirtualControl::new(w - 235.0, bottom - 140.0, 100.0, 140.0, SHAPE_PEDAL, self.key_brake));
            self.controls.push(VirtualControl::new(w - 110.0, bottom - 290.0, 80.0, 80.0, SHAPE_ROUND, self.key_nitro));
            self.controls.push(VirtualControl::new(w - 225.0, bottom - 230.0, 75.0, 75.0, SHAPE_ROUND, self.key_hb));
        } else {
            // Menu Mode (DPad + OK / Back)
            let pad_size = 72.0;
            let cx = 120.0;
            let cy = bottom - 120.0;
            self.controls.push(VirtualControl::new(cx - pad_size * 0.5, cy - pad_size * 1.5, pad_size, pad_size, SHAPE_DPAD, SCAN_UP));
            self.controls.push(VirtualControl::new(cx - pad_size * 0.5, cy + pad_size * 0.5, pad_size, pad_size, SHAPE_DPAD, SCAN_DOWN));
            self.controls.push(VirtualControl::new(cx - pad_size * 1.5, cy - pad_size * 0.5, pad_size, pad_size, SHAPE_DPAD, SCAN_LEFT));
            self.controls.push(VirtualControl::new(cx + pad_size * 0.5, cy - pad_size * 0.5, pad_size, pad_size, SHAPE_DPAD, SCAN_RIGHT));

            // Accept & Back Pills
            self.controls.push(VirtualControl::new(w - 180.0, bottom - 70.0, 150.0, 60.0, SHAPE_PILL, SCAN_ENTER));
            self.controls.push(VirtualControl::new(w - 180.0, bottom - 145.0, 150.0, 60.0, SHAPE_PILL, SCAN_ESC));
        }
    }

    pub fn pointer_down(&mut self, finger_id: usize, x: f32, y: f32) {
        if finger_id >= MAX_FINGERS {
            return;
        }

        let mut hit = None;
        for (idx, ctrl) in self.controls.iter_mut().enumerate() {
            if ctrl.contains(x, y) {
                ctrl.held_count += 1;
                ctrl.active_finger = finger_id as i32;
                hit = Some((idx, ctrl.scan));
                break;
            }
        }
        if let Some((idx, scan)) = hit {
            self.finger_map[finger_id] = Some(idx);
            self.apply_key_down(scan);
        }
    }

    pub fn pointer_move(&mut self, finger_id: usize, x: f32, _y: f32) {
        if finger_id >= MAX_FINGERS {
            return;
        }

        let mut steering = None;
        if let Some(ctrl_idx) = self.finger_map[finger_id] {
            if let Some(ctrl) = self.controls.get_mut(ctrl_idx) {
                if ctrl.scan == CTRL_STICK {
                    let (cx, _) = ctrl.center();
                    let radius = ctrl.w * 0.5;
                    let delta = (x - cx) / radius;
                    let st = delta.clamp(-1.0, 1.0);
                    ctrl.steering = st;
                    steering = Some(st);
                }
            }
        }
        if let Some(st) = steering {
            self.apply_steering_keys(st);
        }
    }

    pub fn pointer_up(&mut self, finger_id: usize) {
        if finger_id >= MAX_FINGERS {
            return;
        }

        let mut key_to_release = None;
        if let Some(ctrl_idx) = self.finger_map[finger_id].take() {
            if let Some(ctrl) = self.controls.get_mut(ctrl_idx) {
                if ctrl.held_count > 0 {
                    ctrl.held_count -= 1;
                    if ctrl.held_count == 0 {
                        ctrl.active_finger = -1;
                        key_to_release = Some(ctrl.scan);
                    }
                }
            }
        }
        if let Some(scan) = key_to_release {
            self.apply_key_up(scan);
        }
    }

    pub fn release_all(&mut self) {
        for slot in &mut self.finger_map {
            *slot = None;
        }
        for ctrl in &mut self.controls {
            ctrl.held_count = 0;
            ctrl.active_finger = -1;
            ctrl.steering = 0.0;
        }
        for key in &mut self.key_states {
            *key = false;
        }
    }

    #[inline]
    fn apply_key_down(&mut self, scan: i32) {
        if scan >= 0 && (scan as usize) < self.key_states.len() {
            self.key_states[scan as usize] = true;
        }
    }

    #[inline]
    fn apply_key_up(&mut self, scan: i32) {
        if scan >= 0 && (scan as usize) < self.key_states.len() {
            self.key_states[scan as usize] = false;
        }
    }

    #[inline]
    fn apply_steering_keys(&mut self, steer: f32) {
        let left_down = steer < -0.18;
        let right_down = steer > 0.18;
        if self.key_left >= 0 && (self.key_left as usize) < self.key_states.len() {
            self.key_states[self.key_left as usize] = left_down;
        }
        if self.key_right >= 0 && (self.key_right as usize) < self.key_states.len() {
            self.key_states[self.key_right as usize] = right_down;
        }
    }

    pub fn is_key_down(&self, scan: i32) -> bool {
        if scan >= 0 && (scan as usize) < self.key_states.len() {
            self.key_states[scan as usize]
        } else {
            false
        }
    }

    pub fn generate_quads(&self, out_quads: &mut Vec<HudQuad>) {
        out_quads.clear();
        let alpha = (self.opacity * 255.0) as u32;

        for ctrl in &self.controls {
            let is_held = ctrl.held_count > 0;
            // var(--lime) #c8f088 when pressed, dark translucent when idle
            let color = if is_held {
                (alpha << 24) | 0x0088F0C8 // Lime tint ABGR
            } else {
                ((alpha / 2) << 24) | 0x00201814 // Dark surface ABGR
            };

            let x0 = ctrl.x;
            let y0 = ctrl.y;
            let x1 = ctrl.x + ctrl.w;
            let y1 = ctrl.y + ctrl.h;

            out_quads.push(HudQuad {
                v0: HudVertex { x: x0, y: y0, u: 0.0, v: 0.0, color_rgba: color },
                v1: HudVertex { x: x1, y: y0, u: 1.0, v: 0.0, color_rgba: color },
                v2: HudVertex { x: x1, y: y1, u: 1.0, v: 1.0, color_rgba: color },
                v3: HudVertex { x: x0, y: y1, u: 0.0, v: 1.0, color_rgba: color },
            });
        }
    }
}

static HUD_GLOBAL: RwLock<Option<TouchHudEngine>> = RwLock::new(None);

fn with_hud<F, R>(f: F) -> R
where
    F: FnOnce(&mut TouchHudEngine) -> R,
{
    let mut guard = HUD_GLOBAL.write().unwrap();
    if guard.is_none() {
        let mut hud = TouchHudEngine::new();
        hud.rebuild_layout();
        *guard = Some(hud);
    }
    f(guard.as_mut().unwrap())
}

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_init(w: f32, h: f32) {
    with_hud(|hud| hud.resize(w, h));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_resize(w: f32, h: f32) {
    with_hud(|hud| hud.resize(w, h));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_pointer_down(finger_id: u32, x: f32, y: f32) {
    with_hud(|hud| hud.pointer_down(finger_id as usize, x, y));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_pointer_move(finger_id: u32, x: f32, y: f32) {
    with_hud(|hud| hud.pointer_move(finger_id as usize, x, y));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_pointer_up(finger_id: u32) {
    with_hud(|hud| hud.pointer_up(finger_id as usize));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_release_all() {
    with_hud(|hud| hud.release_all());
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_is_key_down(scan: i32) -> u32 {
    let down = with_hud(|hud| hud.is_key_down(scan));
    if down { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_set_racing_mode(racing: u32) {
    with_hud(|hud| hud.set_racing_mode(racing != 0));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_set_opacity(opacity: f32) {
    with_hud(|hud| hud.set_opacity(opacity));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_sync_keys(key_cb: Option<unsafe extern "C" fn(u32, u32)>) {
    if let Some(cb) = key_cb {
        with_hud(|hud| {
            for (scan, &down) in hud.key_states.iter().enumerate() {
                cb(scan as u32, if down { 1 } else { 0 });
            }
        });
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_touch_hud_process_motion(
    action: i32,
    pointer_id: i32,
    x: f32,
    y: f32,
    key_cb: Option<unsafe extern "C" fn(u32, u32)>,
) {
    let action_masked = action & 0xFF;
    with_hud(|hud| {
        match action_masked {
            0 | 5 => {
                hud.pointer_down(pointer_id as usize, x, y);
            }
            1 | 6 => {
                hud.pointer_up(pointer_id as usize);
            }
            2 => {
                hud.pointer_move(pointer_id as usize, x, y);
            }
            3 => {
                hud.release_all();
            }
            _ => {}
        }

        if let Some(cb) = key_cb {
            for (scan, &down) in hud.key_states.iter().enumerate() {
                cb(scan as u32, if down { 1 } else { 0 });
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Auto-Dim Engine and Progressive S-Curve Steering
// ---------------------------------------------------------------------------

pub struct AutoDimEngine {
    last_interaction_ms: AtomicU64,
    gamepad_active: AtomicBool,
    current_dim_x1000: AtomicI32,
}

impl AutoDimEngine {
    pub const fn new() -> Self {
        Self {
            last_interaction_ms: AtomicU64::new(0),
            gamepad_active: AtomicBool::new(false),
            current_dim_x1000: AtomicI32::new(1000),
        }
    }

    pub fn record_touch(&self, now_ms: u64) {
        self.last_interaction_ms.store(now_ms, Ordering::Release);
        self.gamepad_active.store(false, Ordering::Release);
        self.current_dim_x1000.store(1000, Ordering::Release);
    }

    pub fn set_gamepad_active(&self, active: bool) {
        self.gamepad_active.store(active, Ordering::Release);
    }

    pub fn update_dim(&self, now_ms: u64, idle_timeout_ms: u64, min_opacity: f32) -> f32 {
        let last = self.last_interaction_ms.load(Ordering::Acquire);
        let gamepad = self.gamepad_active.load(Ordering::Acquire);
        let mut cur = self.current_dim_x1000.load(Ordering::Acquire) as f32 / 1000.0;

        let target = if gamepad || (last > 0 && now_ms.saturating_sub(last) > idle_timeout_ms) {
            min_opacity.clamp(0.0, 0.40)
        } else {
            1.0
        };

        if (cur - target).abs() > 0.01 {
            cur += (target - cur) * 0.12;
            self.current_dim_x1000.store((cur * 1000.0) as i32, Ordering::Release);
        } else {
            cur = target;
            self.current_dim_x1000.store((target * 1000.0) as i32, Ordering::Release);
        }

        cur
    }

    pub fn reset(&self) {
        self.last_interaction_ms.store(0, Ordering::Release);
        self.gamepad_active.store(false, Ordering::Release);
        self.current_dim_x1000.store(1000, Ordering::Release);
    }
}

pub static GLOBAL_AUTO_DIM: AutoDimEngine = AutoDimEngine::new();

/// Calculates progressive S-curve steering deflection for touch controls.
#[inline]
pub fn calculate_scurve_steering(raw_x: f32, center_x: f32, half_width: f32, scale: f32, deadzone: f32) -> f32 {
    if scale <= 0.0 || half_width <= 0.0 {
        return 0.0;
    }
    let norm = ((raw_x / scale) - center_x) / half_width;
    let clamped = norm.clamp(-1.0, 1.0);
    let abs_val = clamped.abs();
    let dz = deadzone.clamp(0.0, 0.40);
    if abs_val <= dz {
        return 0.0;
    }
    let scaled = (abs_val - dz) / (1.0 - dz);
    // S-curve: 35% linear + 65% cubic
    let curved = 0.35 * scaled + 0.65 * scaled * scaled * scaled;
    if clamped < 0.0 { -curved } else { curved }
}

#[no_mangle]
pub extern "C" fn nfs_touch_scurve(raw_x: f32, center_x: f32, half_width: f32, scale: f32, deadzone: f32) -> f32 {
    calculate_scurve_steering(raw_x, center_x, half_width, scale, deadzone)
}

#[no_mangle]
pub extern "C" fn nfs_autodim_touch(now_ms: u64) {
    GLOBAL_AUTO_DIM.record_touch(now_ms);
}

#[no_mangle]
pub extern "C" fn nfs_autodim_set_gamepad(active: i32) {
    GLOBAL_AUTO_DIM.set_gamepad_active(active != 0);
}

#[no_mangle]
pub extern "C" fn nfs_autodim_update(now_ms: u64, idle_timeout_ms: u64, min_opacity: f32) -> f32 {
    GLOBAL_AUTO_DIM.update_dim(now_ms, idle_timeout_ms, min_opacity)
}

#[no_mangle]
pub extern "C" fn nfs_autodim_reset() {
    GLOBAL_AUTO_DIM.reset();
}
