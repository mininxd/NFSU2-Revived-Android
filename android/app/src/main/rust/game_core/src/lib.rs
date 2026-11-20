//! Modular Game Core: Analog controls, right-stick camera orbit, and garage viewer rotation engine for NFSU2 Android.

pub mod camera_stick;
pub mod contrast_regulator;
pub mod dinput_bridge;
pub mod frame_metrics;
pub mod garage_viewer;
pub mod lightroom_contrast;
pub mod menu_gesture;
pub mod nitro_manager;
pub mod tilt_fusion;
pub mod touch_hud;

pub use camera_stick::*;
pub use contrast_regulator::*;
pub use dinput_bridge::*;
pub use frame_metrics::*;
pub use garage_viewer::*;
pub use lightroom_contrast::*;
pub use menu_gesture::*;
pub use nitro_manager::*;
pub use tilt_fusion::*;
pub use touch_hud::*;

use jni::objects::{JClass, JFloatArray};
use jni::sys::{jboolean, jfloat, jfloatArray, jint, jlong};
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
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeGarageZoom(
    _env: JNIEnv,
    _class: JClass,
    delta: jfloat,
) {
    nfs_garage_zoom(delta);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeGarageResetView(
    _env: JNIEnv,
    _class: JClass,
) {
    nfs_garage_reset_view();
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeGarageZoom(
    env: JNIEnv,
    class: JClass,
    delta: jfloat,
) {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeGarageZoom(env, class, delta);
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeGarageResetView(
    env: JNIEnv,
    class: JClass,
) {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeGarageResetView(env, class);
}

// ---------------------------------------------------------------------------
// Tilt Fusion JNI
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeTiltProcess(
    _env: JNIEnv,
    _class: JClass,
    raw_horizontal: jfloat,
    sensitivity: jfloat,
    invert: jboolean,
    deadzone: jfloat,
) -> jfloat {
    nfs_tilt_process(raw_horizontal, sensitivity, invert as i32, deadzone)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeTiltCalibrate(
    _env: JNIEnv,
    _class: JClass,
) {
    nfs_tilt_calibrate();
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeTiltReset(
    _env: JNIEnv,
    _class: JClass,
) {
    nfs_tilt_reset();
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativeTiltProcess(
    env: JNIEnv,
    class: JClass,
    raw_horizontal: jfloat,
    sensitivity: jfloat,
    invert: jboolean,
    deadzone: jfloat,
) -> jfloat {
    Java_io_mininxd_nfsu2_GameActivity_nativeTiltProcess(env, class, raw_horizontal, sensitivity, invert, deadzone)
}

// ---------------------------------------------------------------------------
// Touch S-Curve & Auto-Dim JNI
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeCalculateSCurve(
    _env: JNIEnv,
    _class: JClass,
    raw_x: jfloat,
    center_x: jfloat,
    half_width: jfloat,
    scale: jfloat,
    deadzone: jfloat,
) -> jfloat {
    nfs_touch_scurve(raw_x, center_x, half_width, scale, deadzone)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeAutoDimTouch(
    _env: JNIEnv,
    _class: JClass,
    now_ms: jlong,
) {
    nfs_autodim_touch(now_ms as u64);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeAutoDimSetGamepad(
    _env: JNIEnv,
    _class: JClass,
    active: jboolean,
) {
    nfs_autodim_set_gamepad(active as i32);
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeAutoDimUpdate(
    _env: JNIEnv,
    _class: JClass,
    now_ms: jlong,
    idle_timeout_ms: jlong,
    min_opacity: jfloat,
) -> jfloat {
    nfs_autodim_update(now_ms as u64, idle_timeout_ms as u64, min_opacity)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeAutoDimReset(
    _env: JNIEnv,
    _class: JClass,
) {
    nfs_autodim_reset();
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_RacingControlsView_nativeCalculateSCurve(
    env: JNIEnv,
    class: JClass,
    raw_x: jfloat,
    center_x: jfloat,
    half_width: jfloat,
    scale: jfloat,
    deadzone: jfloat,
) -> jfloat {
    Java_io_mininxd_nfsu2_RacingControlsView_nativeCalculateSCurve(env, class, raw_x, center_x, half_width, scale, deadzone)
}

// ---------------------------------------------------------------------------
// Gamepad DirectInput Bridge JNI
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeDInputRadialX(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
    curve_weight: jfloat,
) -> jfloat {
    nfs_dinput_radial_x(x, y, deadzone, curve_weight)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_GameActivity_nativeDInputRadialY(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
    curve_weight: jfloat,
) -> jfloat {
    nfs_dinput_radial_y(x, y, deadzone, curve_weight)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativeDInputRadialX(
    env: JNIEnv,
    class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
    curve_weight: jfloat,
) -> jfloat {
    Java_io_mininxd_nfsu2_GameActivity_nativeDInputRadialX(env, class, x, y, deadzone, curve_weight)
}

#[no_mangle]
pub extern "system" fn Java_com_nfsu2_androidevolved_GameActivity_nativeDInputRadialY(
    env: JNIEnv,
    class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
    curve_weight: jfloat,
) -> jfloat {
    Java_io_mininxd_nfsu2_GameActivity_nativeDInputRadialY(env, class, x, y, deadzone, curve_weight)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeDInputRadialX(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
    curve_weight: jfloat,
) -> jfloat {
    nfs_dinput_radial_x(x, y, deadzone, curve_weight)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeDInputRadialY(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
    curve_weight: jfloat,
) -> jfloat {
    nfs_dinput_radial_y(x, y, deadzone, curve_weight)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_RacingControlsView_nativeDInputTrigger(
    _env: JNIEnv,
    _class: JClass,
    raw: jfloat,
    deadzone: jfloat,
) -> jfloat {
    nfs_dinput_trigger(raw, deadzone)
}

// ---------------------------------------------------------------------------
// Frame Metrics JNI
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_DiagnosticOverlay_nativeRecordFrameDuration(
    _env: JNIEnv,
    _class: JClass,
    duration_ns: jlong,
) {
    if duration_ns > 0 {
        nfs_metrics_record_frame(duration_ns as u64);
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_DiagnosticOverlay_nativeGetFrameMetrics(
    mut env: JNIEnv,
    _class: JClass,
    out_array: jfloatArray,
) -> jboolean {
    if out_array.is_null() {
        return 0;
    }
    let mut metrics = [0.0f32; 6];
    GLOBAL_FRAME_METRICS.compute_metrics(&mut metrics);
    let j_arr = JFloatArray::from_raw(out_array);
    if env.set_float_array_region(&j_arr, 0, &metrics).is_ok() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_com_nfsu2_androidevolved_DiagnosticOverlay_nativeGetFrameMetrics(
    env: JNIEnv,
    class: JClass,
    out_array: jfloatArray,
) -> jboolean {
    Java_io_mininxd_nfsu2_DiagnosticOverlay_nativeGetFrameMetrics(env, class, out_array)
}
