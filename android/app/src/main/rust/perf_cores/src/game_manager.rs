//! Android GameManager & GameState Platform Integration in Safe Rust.
//! Signals active uninterruptible gameplay mode to prevent compositor downthrottling
//! to 30Hz and optimizes display scheduling on Android 12/13/14+.

use jni::objects::{JClass, JObject, JValue};
use jni::sys::jint;
use jni::JNIEnv;

const FLAG_KEEP_SCREEN_ON: i32 = 0x00000080;
const MODE_GAMEPLAY_UNINTERRUPTIBLE: i32 = 2;
const MODE_CONTENT: i32 = 4;
const MODE_NONE: i32 = 0;

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_AndroidGameManagerBridge_nativeSetGameplayMode(
    mut env: JNIEnv,
    _class: JClass,
    activity: JObject,
    target_fps: jint,
) {
    if activity.is_null() {
        return;
    }

    // 1. Keep display awake: activity.getWindow().addFlags(FLAG_KEEP_SCREEN_ON)
    if let Ok(window_val) = env.call_method(&activity, "getWindow", "()Landroid/view/Window;", &[]) {
        if let Ok(window) = window_val.l() {
            if !window.is_null() {
                let _ = env.call_method(
                    &window,
                    "addFlags",
                    "(I)V",
                    &[JValue::Int(FLAG_KEEP_SCREEN_ON)],
                );
            }
        }
    }
    let _ = env.exception_clear();

    // 2. Query SDK_INT from android.os.Build.VERSION
    let sdk_int = get_sdk_int(&mut env);
    let fps = if target_fps > 0 { target_fps } else { 60 };

    if sdk_int >= 33 {
        // Android 13+ (Tiramisu): GameManager.setGameState(GameState)
        set_game_state_uninterruptible(&mut env, &activity, fps);
    } else if sdk_int >= 31 {
        // Android 12 (S): check GameManager mode
        log_android_12_game_mode(&mut env, &activity);
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_AndroidGameManagerBridge_nativeSetLoadingMode(
    mut env: JNIEnv,
    _class: JClass,
    activity: JObject,
) {
    if activity.is_null() {
        return;
    }
    let sdk_int = get_sdk_int(&mut env);
    if sdk_int >= 33 {
        set_game_state_loading(&mut env, &activity);
    }
}

fn get_sdk_int(env: &mut JNIEnv) -> i32 {
    if let Ok(version_class) = env.find_class("android/os/Build$VERSION") {
        if let Ok(sdk_field) = env.get_static_field_id(&version_class, "SDK_INT", "I") {
            if let Ok(val) = env.get_static_field_unchecked(&version_class, sdk_field, jni::signature::JavaType::Primitive(jni::signature::Primitive::Int)) {
                if let Ok(i) = val.i() {
                    return i;
                }
            }
        }
    }
    let _ = env.exception_clear();
    0
}

fn set_game_state_uninterruptible(env: &mut JNIEnv, activity: &JObject, fps: i32) {
    let game_svc_str = match env.new_string("game") {
        Ok(s) => s,
        Err(_) => return,
    };
    let gm_val = match env.call_method(
        activity,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        &[JValue::Object(&game_svc_str)],
    ) {
        Ok(v) => v,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };
    let gm = match gm_val.l() {
        Ok(o) if !o.is_null() => o,
        _ => return,
    };

    // new GameState(false, MODE_GAMEPLAY_UNINTERRUPTIBLE, MODE_NONE, fps)
    let state_class = match env.find_class("android/app/GameState") {
        Ok(c) => c,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };
    let state_obj = match env.new_object(
        &state_class,
        "(ZIII)V",
        &[
            JValue::Bool(0), // isLoading = false
            JValue::Int(MODE_GAMEPLAY_UNINTERRUPTIBLE),
            JValue::Int(MODE_NONE),
            JValue::Int(fps),
        ],
    ) {
        Ok(o) => o,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };

    let _ = env.call_method(
        &gm,
        "setGameState",
        "(Landroid/app/GameState;)V",
        &[JValue::Object(&state_obj)],
    );
    let _ = env.exception_clear();
}

fn set_game_state_loading(env: &mut JNIEnv, activity: &JObject) {
    let game_svc_str = match env.new_string("game") {
        Ok(s) => s,
        Err(_) => return,
    };
    let gm_val = match env.call_method(
        activity,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        &[JValue::Object(&game_svc_str)],
    ) {
        Ok(v) => v,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };
    let gm = match gm_val.l() {
        Ok(o) if !o.is_null() => o,
        _ => return,
    };

    let state_class = match env.find_class("android/app/GameState") {
        Ok(c) => c,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };
    let state_obj = match env.new_object(
        &state_class,
        "(ZI)V",
        &[
            JValue::Bool(1), // isLoading = true
            JValue::Int(MODE_CONTENT),
        ],
    ) {
        Ok(o) => o,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };

    let _ = env.call_method(
        &gm,
        "setGameState",
        "(Landroid/app/GameState;)V",
        &[JValue::Object(&state_obj)],
    );
    let _ = env.exception_clear();
}

fn log_android_12_game_mode(env: &mut JNIEnv, activity: &JObject) {
    let game_svc_str = match env.new_string("game") {
        Ok(s) => s,
        Err(_) => return,
    };
    let gm_val = match env.call_method(
        activity,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        &[JValue::Object(&game_svc_str)],
    ) {
        Ok(v) => v,
        Err(_) => {
            let _ = env.exception_clear();
            return;
        }
    };
    let gm = match gm_val.l() {
        Ok(o) if !o.is_null() => o,
        _ => return,
    };
    let _ = env.call_method(&gm, "getGameMode", "()I", &[]);
    let _ = env.exception_clear();
}
