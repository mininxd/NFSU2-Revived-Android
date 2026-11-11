//! Garage & Frontend Car Viewer Rotation Engine (Analog Stick & Two-Finger Drag).
//!
//! Replicates console (PS2/Xbox) analog stick car viewing and `nfsu2-web` two-finger drag
//! inside Need for Speed Underground 2's car showroom, garage, and performance shops.
//!
//! Technical architecture:
//! - Front-end CarViewer message loop (0x4d8d20 -> 0x4d82d0 update -> 0x4a5d90 viewer).
//! - Offsets within CarViewer object:
//!   - VW_CAMERA (0x23c): Pointer to camera object
//!   - VW_TURN (0x248): Float turn angular velocity (60 deg/sec at 1.0)
//!   - VW_TILT (0x24c): Float camera tilt velocity
//!   - VW_ANGLE (0x274): Float current car yaw in degrees
//! - Global Game Timers & State:
//!   - G_FE_BUSY (0x00838578): Front-end busy flag (modal dialogs)
//!   - G_MOUSE_MID (0x008763f7): Middle mouse button down flag
//!   - G_SHOWCASE (0x008363fc): Idle showcase rotation active flag
//!   - G_VIEW_IDLE (0x00837aa0): Timestamp of last user viewer manipulation
//!   - G_REALTIMER (0x008651ac): Game real-time clock

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, Ordering};
use std::sync::Mutex;
use std::time::Instant;

pub const VW_CAMERA: u32 = 0x23c;
pub const VW_TURN: u32 = 0x248;
pub const VW_TILT: u32 = 0x24c;
pub const VW_ANGLE: u32 = 0x274;
pub const CAM_HEIGHT: u32 = 0xa8;

pub const G_FE_BUSY: u32 = 0x00838578;
pub const G_MOUSE_MID: u32 = 0x008763f7;
pub const G_SHOWCASE: u32 = 0x008363fc;
pub const G_VIEW_IDLE: u32 = 0x00837aa0;
pub const G_REALTIMER: u32 = 0x008651ac;

const STICK_DEADZONE: f32 = 0.15;

pub struct GarageViewerEngine {
    stick_x: AtomicI32, // (f32 * 10000.0) as i32
    stick_y: AtomicI32,
    two_finger_active: AtomicBool,
    two_finger_dx: AtomicI32,
    two_finger_dy: AtomicI32,
    last_two_finger_update: Mutex<Option<Instant>>,
    last_viewer_addr: AtomicU32,
}

impl GarageViewerEngine {
    pub const fn new() -> Self {
        Self {
            stick_x: AtomicI32::new(0),
            stick_y: AtomicI32::new(0),
            two_finger_active: AtomicBool::new(false),
            two_finger_dx: AtomicI32::new(0),
            two_finger_dy: AtomicI32::new(0),
            last_two_finger_update: Mutex::new(None),
            last_viewer_addr: AtomicU32::new(0),
        }
    }

    /// Sets right analog stick input from gamepad or onscreen controls.
    pub fn set_stick_input(&self, x: f32, y: f32) {
        let ix = (x.clamp(-1.0, 1.0) * 10000.0) as i32;
        let iy = (y.clamp(-1.0, 1.0) * 10000.0) as i32;
        self.stick_x.store(ix, Ordering::Release);
        self.stick_y.store(iy, Ordering::Release);
    }

    /// Sets two-finger gesture touch drag.
    /// `dx` and `dy` are normalized screen motion deltas.
    pub fn set_two_finger_drag(&self, dx: f32, dy: f32, active: bool) {
        self.two_finger_active.store(active, Ordering::Release);
        if active {
            let idx = (dx * 10000.0) as i32;
            let idy = (dy * 10000.0) as i32;
            self.two_finger_dx.store(idx, Ordering::Release);
            self.two_finger_dy.store(idy, Ordering::Release);
            if let Ok(mut guard) = self.last_two_finger_update.lock() {
                *guard = Some(Instant::now());
            }
        } else {
            self.two_finger_dx.store(0, Ordering::Release);
            self.two_finger_dy.store(0, Ordering::Release);
        }
    }

    #[inline]
    pub fn get_stick_input(&self) -> (f32, f32) {
        let ix = self.stick_x.load(Ordering::Acquire);
        let iy = self.stick_y.load(Ordering::Acquire);
        (ix as f32 / 10000.0, iy as f32 / 10000.0)
    }

    /// Called from the hooked viewer procedure (sub_004a5d90) each frame.
    /// `viewer_va` is the guest pointer (ECX) to CarViewer.
    /// `guest_base` is the native base of guest x86 address space.
    pub unsafe fn update_viewer(&self, viewer_va: u32, guest_base: *mut u8) {
        if guest_base.is_null() || viewer_va < 0x10000 || viewer_va >= 0x7fff0000 {
            return;
        }

        self.last_viewer_addr.store(viewer_va, Ordering::Release);

        // Check if middle mouse button is down in game memory. If PC mouse drag is active,
        // defer to the game's native drag handler.
        let mouse_mid_ptr = guest_base.add(G_MOUSE_MID as usize);
        if *mouse_mid_ptr != 0 {
            return;
        }

        // Check if frontend is busy (e.g. GenericDialog.fng is up)
        let fe_busy_ptr = guest_base.add(G_FE_BUSY as usize) as *const u32;
        if *fe_busy_ptr != 0 {
            let turn_ptr = guest_base.add((viewer_va + VW_TURN) as usize) as *mut f32;
            let tilt_ptr = guest_base.add((viewer_va + VW_TILT) as usize) as *mut f32;
            *turn_ptr = 0.0;
            *tilt_ptr = 0.0;
            return;
        }

        let mut turn_vel = 0.0f32;
        let mut tilt_vel = 0.0f32;

        // 1. Process two-finger drag
        if self.two_finger_active.load(Ordering::Acquire) {
            let idx = self.two_finger_dx.swap(0, Ordering::AcqRel);
            let idy = self.two_finger_dy.swap(0, Ordering::AcqRel);
            let dx = idx as f32 / 10000.0;
            let dy = idy as f32 / 10000.0;

            // Sensitivity multiplier for two-finger drag: fast responsive rotation
            turn_vel = dx * 8.0;
            tilt_vel = dy * 8.0;
        }

        // 2. Process right analog stick if two-finger drag is not dominating
        if turn_vel.abs() < 0.001 && tilt_vel.abs() < 0.001 {
            let (sx, sy) = self.get_stick_input();
            let mag = (sx * sx + sy * sy).sqrt();
            if mag >= STICK_DEADZONE {
                turn_vel = sx;
                tilt_vel = sy;
            }
        }

        let turn_ptr = guest_base.add((viewer_va + VW_TURN) as usize) as *mut f32;
        let tilt_ptr = guest_base.add((viewer_va + VW_TILT) as usize) as *mut f32;

        if turn_vel.abs() < 0.001 && tilt_vel.abs() < 0.001 {
            *turn_ptr = 0.0;
            *tilt_ptr = 0.0;
            return;
        }

        *turn_ptr = turn_vel.clamp(-2.0, 2.0);
        *tilt_ptr = tilt_vel.clamp(-2.0, 2.0);

        // Reset idle showcase and update idle clock so the game does not start
        // auto-rotating the car while the user is actively viewing it
        let showcase_ptr = guest_base.add(G_SHOWCASE as usize);
        *showcase_ptr = 0;

        let realtimer_ptr = guest_base.add(G_REALTIMER as usize) as *const u32;
        let idle_ptr = guest_base.add(G_VIEW_IDLE as usize) as *mut u32;
        *idle_ptr = *realtimer_ptr;
    }
}

pub static GLOBAL_GARAGE_VIEWER: GarageViewerEngine = GarageViewerEngine::new();

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_garage_stick_input(x: f32, y: f32) {
    GLOBAL_GARAGE_VIEWER.set_stick_input(x, y);
}

#[no_mangle]
pub extern "C" fn nfs_garage_two_finger_drag(dx: f32, dy: f32, active: i32) {
    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, active != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_garage_apply_viewer(viewer_va: u32, guest_base: *mut std::ffi::c_void) {
    GLOBAL_GARAGE_VIEWER.update_viewer(viewer_va, guest_base as *mut u8);
}
