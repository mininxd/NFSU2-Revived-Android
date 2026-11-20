//! Modular Game Core: Analog controls, right-stick camera orbit, and garage viewer rotation engine for NFSU2 Android.

pub mod camera_stick;
pub mod contrast_regulator;
pub mod garage_viewer;
pub mod lightroom_contrast;
pub mod menu_gesture;
pub mod nitro_manager;
pub mod touch_hud;

pub use camera_stick::*;
pub use contrast_regulator::*;
pub use garage_viewer::*;
pub use lightroom_contrast::*;
pub use menu_gesture::*;
pub use nitro_manager::*;
pub use touch_hud::*;

use jni::objects::JClass;
use jni::sys::{jboolean, jfloat, jint, jlong};
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

// ---------------------------------------------------------------------------
// Menu & Garage 1-Finger Gesture Disambiguation (Hold >100ms vs Click)
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureProcess(
    _env: JNIEnv,
    _class: JClass,
    action: jint,
    pointer_id: jint,
    x: jfloat,
    y: jfloat,
    time_ms: jlong,
    view_w: jfloat,
    view_h: jfloat,
) -> jint {
    let t = if time_ms > 0 { time_ms as u64 } else { 0 };
    nfs_menu_gesture_process(action, pointer_id, x, y, t, view_w, view_h)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureCheckTimeout(
    _env: JNIEnv,
    _class: JClass,
    time_ms: jlong,
) -> jint {
    let t = if time_ms > 0 { time_ms as u64 } else { 0 };
    nfs_menu_gesture_check_timeout(t)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureReset(
    _env: JNIEnv,
    _class: JClass,
) {
    nfs_menu_gesture_reset();
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetX(
    _env: JNIEnv,
    _class: JClass,
) -> jfloat {
    nfs_menu_gesture_get_x()
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetY(
    _env: JNIEnv,
    _class: JClass,
) -> jfloat {
    nfs_menu_gesture_get_y()
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetDx(
    _env: JNIEnv,
    _class: JClass,
) -> jfloat {
    nfs_menu_gesture_get_dx()
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetDy(
    _env: JNIEnv,
    _class: JClass,
) -> jfloat {
    nfs_menu_gesture_get_dy()
}

// com.nfsu2.androidevolved package aliases
#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureProcess(
    env: JNIEnv,
    class: JClass,
    action: jint,
    pointer_id: jint,
    x: jfloat,
    y: jfloat,
    time_ms: jlong,
    view_w: jfloat,
    view_h: jfloat,
) -> jint {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureProcess(env, class, action, pointer_id, x, y, time_ms, view_w, view_h)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureCheckTimeout(
    env: JNIEnv,
    class: JClass,
    time_ms: jlong,
) -> jint {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureCheckTimeout(env, class, time_ms)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureReset(
    env: JNIEnv,
    class: JClass,
) {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureReset(env, class);
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureGetX(
    env: JNIEnv,
    class: JClass,
) -> jfloat {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetX(env, class)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureGetY(
    env: JNIEnv,
    class: JClass,
) -> jfloat {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetY(env, class)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureGetDx(
    env: JNIEnv,
    class: JClass,
) -> jfloat {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetDx(env, class)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeMenuGestureGetDy(
    env: JNIEnv,
    class: JClass,
) -> jfloat {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeMenuGestureGetDy(env, class)
}
