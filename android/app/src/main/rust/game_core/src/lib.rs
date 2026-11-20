//! Modular Game Core: Analog controls, right-stick camera orbit, and garage viewer rotation engine for NFSU2 Android.

pub mod camera_stick;
pub mod contrast_regulator;
pub mod garage_viewer;
pub mod lightroom_contrast;
pub mod nitro_manager;
pub mod touch_hud;

pub use camera_stick::*;
pub use contrast_regulator::*;
pub use garage_viewer::*;
pub use lightroom_contrast::*;
pub use nitro_manager::*;
pub use touch_hud::*;

use jni::objects::JClass;
use jni::sys::{jboolean, jfloat, jint};
use jni::JNIEnv;

// ---------------------------------------------------------------------------
// JNI Bindings for direct Java -> game_core acceleration
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeCameraStickInput(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
) {
    GLOBAL_CAMERA_STICK.set_stick_input(x, y);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeGarageStickInput(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
) {
    GLOBAL_GARAGE_VIEWER.set_stick_input(x, y);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeGarageTwoFingerDrag(
    _env: JNIEnv,
    _class: JClass,
    dx: jfloat,
    dy: jfloat,
    active: jboolean,
) {
    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, active != 0);
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativeCameraStickInput(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
) {
    GLOBAL_CAMERA_STICK.set_stick_input(x, y);
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativeGarageStickInput(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
) {
    GLOBAL_GARAGE_VIEWER.set_stick_input(x, y);
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativeGarageTwoFingerDrag(
    _env: JNIEnv,
    _class: JClass,
    dx: jfloat,
    dy: jfloat,
    active: jboolean,
) {
    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, active != 0);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeTouchHudPointerDown(
    _env: JNIEnv,
    _class: JClass,
    finger_id: jint,
    x: jfloat,
    y: jfloat,
) {
    unsafe {
        nfs_touch_hud_pointer_down(finger_id as u32, x, y);
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeTouchHudPointerMove(
    _env: JNIEnv,
    _class: JClass,
    finger_id: jint,
    x: jfloat,
    y: jfloat,
) {
    unsafe {
        nfs_touch_hud_pointer_move(finger_id as u32, x, y);
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeTouchHudPointerUp(
    _env: JNIEnv,
    _class: JClass,
    finger_id: jint,
) {
    unsafe {
        nfs_touch_hud_pointer_up(finger_id as u32);
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeTouchHudIsKeyDown(
    _env: JNIEnv,
    _class: JClass,
    scan: jint,
) -> jboolean {
    unsafe {
        (nfs_touch_hud_is_key_down(scan) != 0) as jboolean
    }
}
