//! Modular Game Core: Analog controls, right-stick camera orbit, and garage viewer rotation engine for NFSU2 Android.

pub mod camera_stick;
pub mod garage_viewer;
pub mod nitro_manager;

pub use camera_stick::*;
pub use garage_viewer::*;
pub use nitro_manager::*;

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
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativePointerMiddle(
    _env: JNIEnv,
    _class: JClass,
    _x: jfloat,
    _y: jfloat,
    phase: jint,
    dx: jfloat,
    dy: jfloat,
) {
    let active = phase != 2;
    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, active);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeDirectPointerMiddle(
    _env: JNIEnv,
    _class: JClass,
    _x: jfloat,
    _y: jfloat,
    phase: jint,
    dx: jfloat,
    dy: jfloat,
) {
    let active = phase != 2;
    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, active);
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
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativePointerMiddle(
    _env: JNIEnv,
    _class: JClass,
    _x: jfloat,
    _y: jfloat,
    phase: jint,
    dx: jfloat,
    dy: jfloat,
) {
    let active = phase != 2;
    GLOBAL_GARAGE_VIEWER.set_two_finger_drag(dx, dy, active);
}
