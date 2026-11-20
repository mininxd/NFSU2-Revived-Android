//! Garage & Menu Touch Gesture Engine in Safe Rust for NFSU2 Android.
//!
//! Implements single-finger hold (>100ms) disambiguation:
//! - Quick Tap (< 100ms, movement <= touch_slop): Dispatches Left Mouse Click (WM_LBUTTONDOWN / UP).
//! - Hold (> 100ms) or Drag (> touch_slop): Dispatches 3D Car Orbit / Showroom Rotation.

use std::sync::Mutex;
use crate::garage_viewer::GLOBAL_GARAGE_VIEWER;

pub const GESTURE_NONE: i32 = 0;
pub const GESTURE_CLICK: i32 = 1;
pub const GESTURE_DRAG_START: i32 = 2;
pub const GESTURE_DRAG_MOVE: i32 = 3;
pub const GESTURE_DRAG_END: i32 = 4;

const HOLD_THRESHOLD_MS: u64 = 100;
const TOUCH_SLOP_PX: f32 = 12.0;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum GestureState {
    Idle,
    Holding,
    Dragging,
}

pub struct MenuGestureDetector {
    state: GestureState,
    pointer_id: i32,
    down_x: f32,
    down_y: f32,
    last_x: f32,
    last_y: f32,
    down_time_ms: u64,
    view_width: f32,
    view_height: f32,

    out_x: f32,
    out_y: f32,
    out_dx: f32,
    out_dy: f32,
}

impl MenuGestureDetector {
    pub const fn new() -> Self {
        Self {
            state: GestureState::Idle,
            pointer_id: -1,
            down_x: 0.0,
            down_y: 0.0,
            last_x: 0.0,
            last_y: 0.0,
            down_time_ms: 0,
            view_width: 1.0,
            view_height: 1.0,
            out_x: 0.0,
            out_y: 0.0,
            out_dx: 0.0,
            out_dy: 0.0,
        }
    }

    pub fn reset(&mut self) {
        if self.state == GestureState::Dragging {
            GLOBAL_GARAGE_VIEWER.set_two_finger_drag(0.0, 0.0, false);
        }
        self.state = GestureState::Idle;
        self.pointer_id = -1;
        self.out_dx = 0.0;
        self.out_dy = 0.0;
    }

    pub fn on_down(
        &mut self,
        pointer_id: i32,
        x: f32,
        y: f32,
        time_ms: u64,
        width: f32,
        height: f32,
    ) {
        if self.state == GestureState::Dragging {
            return;
        }

        self.state = GestureState::Holding;
        self.pointer_id = pointer_id;
        self.down_x = x;
        self.down_y = y;
        self.last_x = x;
        self.last_y = y;
        self.down_time_ms = time_ms;
        self.view_width = if width > 1.0 { width } else { 1.0 };
        self.view_height = if height > 1.0 { height } else { 1.0 };
        self.out_x = x;
        self.out_y = y;
        self.out_dx = 0.0;
        self.out_dy = 0.0;
    }

    pub fn check_timeout(&mut self, time_ms: u64) -> i32 {
        if self.state == GestureState::Holding {
            let elapsed = time_ms.saturating_sub(self.down_time_ms);
            if elapsed >= HOLD_THRESHOLD_MS {
                self.state = GestureState::Dragging;
                self.out_x = self.last_x;
                self.out_y = self.last_y;
                self.out_dx = 0.0;
                self.out_dy = 0.0;
                GLOBAL_GARAGE_VIEWER.set_two_finger_drag(0.0, 0.0, true);
                return GESTURE_DRAG_START;
            }
        }
        GESTURE_NONE
    }

    pub fn on_move(&mut self, pointer_id: i32, x: f32, y: f32, time_ms: u64) -> i32 {
        if self.pointer_id != pointer_id {
            return GESTURE_NONE;
        }

        let elapsed = time_ms.saturating_sub(self.down_time_ms);
        let dist = ((x - self.down_x).powi(2) + (y - self.down_y).powi(2)).sqrt();

        match self.state {
            GestureState::Holding => {
                if elapsed >= HOLD_THRESHOLD_MS || dist >= TOUCH_SLOP_PX {
                    self.state = GestureState::Dragging;
                    let dx = (x - self.last_x) / self.view_width;
                    let dy = (y - self.last_y) / self.view_height;
                    self.last_x = x;
                    self.last_y = y;
                    self.out_x = x;
                    self.out_y = y;
                    self.out_dx = dx;
                    self.out_dy = dy;
                    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, true);
                    GESTURE_DRAG_MOVE
                } else {
                    GESTURE_NONE
                }
            }
            GestureState::Dragging => {
                let dx = (x - self.last_x) / self.view_width;
                let dy = (y - self.last_y) / self.view_height;
                self.last_x = x;
                self.last_y = y;
                self.out_x = x;
                self.out_y = y;
                self.out_dx = dx;
                self.out_dy = dy;
                GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, true);
                GESTURE_DRAG_MOVE
            }
            GestureState::Idle => GESTURE_NONE,
        }
    }

    pub fn on_up(&mut self, pointer_id: i32, x: f32, y: f32, time_ms: u64) -> i32 {
        if self.pointer_id != pointer_id {
            return GESTURE_NONE;
        }

        let elapsed = time_ms.saturating_sub(self.down_time_ms);
        let dist = ((x - self.down_x).powi(2) + (y - self.down_y).powi(2)).sqrt();

        match self.state {
            GestureState::Holding => {
                self.state = GestureState::Idle;
                self.pointer_id = -1;
                if elapsed < HOLD_THRESHOLD_MS && dist < TOUCH_SLOP_PX {
                    self.out_x = self.down_x;
                    self.out_y = self.down_y;
                    self.out_dx = 0.0;
                    self.out_dy = 0.0;
                    GESTURE_CLICK
                } else {
                    GESTURE_NONE
                }
            }
            GestureState::Dragging => {
                self.state = GestureState::Idle;
                self.pointer_id = -1;
                self.out_x = x;
                self.out_y = y;
                self.out_dx = 0.0;
                self.out_dy = 0.0;
                GLOBAL_GARAGE_VIEWER.set_two_finger_drag(0.0, 0.0, false);
                GESTURE_DRAG_END
            }
            GestureState::Idle => GESTURE_NONE,
        }
    }

    pub fn on_cancel(&mut self) -> i32 {
        if self.state == GestureState::Dragging {
            self.state = GestureState::Idle;
            self.pointer_id = -1;
            self.out_x = self.last_x;
            self.out_y = self.last_y;
            self.out_dx = 0.0;
            self.out_dy = 0.0;
            GLOBAL_GARAGE_VIEWER.set_two_finger_drag(0.0, 0.0, false);
            GESTURE_DRAG_END
        } else {
            self.state = GestureState::Idle;
            self.pointer_id = -1;
            GESTURE_NONE
        }
    }
}

pub static GLOBAL_GESTURE: Mutex<MenuGestureDetector> = Mutex::new(MenuGestureDetector::new());

// ---------------------------------------------------------------------------
// C-ABI & JNI Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_process(
    action: i32,
    pointer_id: i32,
    x: f32,
    y: f32,
    time_ms: u64,
    width: f32,
    height: f32,
) -> i32 {
    let action_masked = action & 0xFF;
    if let Ok(mut detector) = GLOBAL_GESTURE.lock() {
        match action_masked {
            0 | 5 => {
                detector.on_down(pointer_id, x, y, time_ms, width, height);
                GESTURE_NONE
            }
            2 => detector.on_move(pointer_id, x, y, time_ms),
            1 | 6 => detector.on_up(pointer_id, x, y, time_ms),
            3 => detector.on_cancel(),
            _ => GESTURE_NONE,
        }
    } else {
        GESTURE_NONE
    }
}

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_check_timeout(time_ms: u64) -> i32 {
    if let Ok(mut detector) = GLOBAL_GESTURE.lock() {
        detector.check_timeout(time_ms)
    } else {
        GESTURE_NONE
    }
}

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_reset() {
    if let Ok(mut detector) = GLOBAL_GESTURE.lock() {
        detector.reset();
    }
}

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_get_x() -> f32 {
    if let Ok(detector) = GLOBAL_GESTURE.lock() {
        detector.out_x
    } else {
        0.0
    }
}

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_get_y() -> f32 {
    if let Ok(detector) = GLOBAL_GESTURE.lock() {
        detector.out_y
    } else {
        0.0
    }
}

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_get_dx() -> f32 {
    if let Ok(detector) = GLOBAL_GESTURE.lock() {
        detector.out_dx
    } else {
        0.0
    }
}

#[no_mangle]
pub extern "C" fn nfs_menu_gesture_get_dy() -> f32 {
    if let Ok(detector) = GLOBAL_GESTURE.lock() {
        detector.out_dy
    } else {
        0.0
    }
}
