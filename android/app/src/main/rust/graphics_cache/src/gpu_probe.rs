//! Pure Rust Dynamic GPU & Graphics Driver Probe for NFSU2 Android.
//!
//! Provides dynamic runtime querying for:
//! - Native OpenGL ES 3.0 / 3.1 / 3.2 via dynamic libEGL.so / libGLESv2.so loading
//! - Accurate GPU renderer, vendor, driver version, and BC texture compression checks
//!
//! Eliminates legacy DXVK strings and provides dynamic, backend-specific diagnostics.

use std::ffi::{c_char, c_int, c_void, CStr, CString};

// --- EGL Types & Constants ---
type EGLDisplay = *mut c_void;
type EGLConfig = *mut c_void;
type EGLContext = *mut c_void;
type EGLSurface = *mut c_void;
type EGLint = i32;
type EGLBoolean = u32;

const EGL_DEFAULT_DISPLAY: *mut c_void = std::ptr::null_mut();
const EGL_NO_CONTEXT: EGLContext = std::ptr::null_mut();
const EGL_NO_SURFACE: EGLSurface = std::ptr::null_mut();
const EGL_OPENGL_ES2_BIT: EGLint = 0x0004;
const EGL_OPENGL_ES3_BIT_KHR: EGLint = 0x0040;
const EGL_SURFACE_TYPE: EGLint = 0x3033;
const EGL_PBUFFER_BIT: EGLint = 0x0001;
const EGL_RENDERABLE_TYPE: EGLint = 0x3040;
const EGL_RED_SIZE: EGLint = 0x3024;
const EGL_GREEN_SIZE: EGLint = 0x3023;
const EGL_BLUE_SIZE: EGLint = 0x3022;
const EGL_ALPHA_SIZE: EGLint = 0x3021;
const EGL_DEPTH_SIZE: EGLint = 0x3025;
const EGL_NONE: EGLint = 0x3038;
const EGL_CONTEXT_CLIENT_VERSION: EGLint = 0x3098;
const EGL_WIDTH: EGLint = 0x3057;
const EGL_HEIGHT: EGLint = 0x3056;

// --- GLES Constants ---
const GL_RENDERER: u32 = 0x1F01;
const GL_VENDOR: u32 = 0x1F00;
const GL_VERSION: u32 = 0x1F02;
const GL_EXTENSIONS: u32 = 0x1F03;

fn escape_json(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(c),
        }
    }
    out
}

/// Dynamically probes the native OpenGL ES subsystem via libEGL and libGLES.
/// Returns a JSON diagnostic report matching the driver report schema.
pub fn probe_gles() -> String {
    unsafe {
        let egl_lib = libc::dlopen(b"libEGL.so\0".as_ptr() as *const c_char, libc::RTLD_NOW);
        if egl_lib.is_null() {
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"Could not load libEGL.so"}"#.to_string();
        }

        let gles_lib = libc::dlopen(b"libGLESv3.so\0".as_ptr() as *const c_char, libc::RTLD_NOW);
        let gles_lib = if gles_lib.is_null() {
            libc::dlopen(b"libGLESv2.so\0".as_ptr() as *const c_char, libc::RTLD_NOW)
        } else {
            gles_lib
        };

        if gles_lib.is_null() {
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"Could not load libGLESv3.so or libGLESv2.so"}"#.to_string();
        }

        type FnGetDisplay = extern "C" fn(*mut c_void) -> EGLDisplay;
        type FnInitialize = extern "C" fn(EGLDisplay, *mut EGLint, *mut EGLint) -> EGLBoolean;
        type FnChooseConfig = extern "C" fn(EGLDisplay, *const EGLint, *mut EGLConfig, EGLint, *mut EGLint) -> EGLBoolean;
        type FnCreateContext = extern "C" fn(EGLDisplay, EGLConfig, EGLContext, *const EGLint) -> EGLContext;
        type FnCreatePbuffer = extern "C" fn(EGLDisplay, EGLConfig, *const EGLint) -> EGLSurface;
        type FnMakeCurrent = extern "C" fn(EGLDisplay, EGLSurface, EGLSurface, EGLContext) -> EGLBoolean;
        type FnDestroySurface = extern "C" fn(EGLDisplay, EGLSurface) -> EGLBoolean;
        type FnDestroyContext = extern "C" fn(EGLDisplay, EGLContext) -> EGLBoolean;
        type FnTerminate = extern "C" fn(EGLDisplay) -> EGLBoolean;
        type FnGetString = extern "C" fn(u32) -> *const c_char;

        let egl_get_display: Option<FnGetDisplay> = std::mem::transmute(libc::dlsym(egl_lib, b"eglGetDisplay\0".as_ptr() as *const c_char));
        let egl_init: Option<FnInitialize> = std::mem::transmute(libc::dlsym(egl_lib, b"eglInitialize\0".as_ptr() as *const c_char));
        let egl_choose_cfg: Option<FnChooseConfig> = std::mem::transmute(libc::dlsym(egl_lib, b"eglChooseConfig\0".as_ptr() as *const c_char));
        let egl_create_ctx: Option<FnCreateContext> = std::mem::transmute(libc::dlsym(egl_lib, b"eglCreateContext\0".as_ptr() as *const c_char));
        let egl_create_pb: Option<FnCreatePbuffer> = std::mem::transmute(libc::dlsym(egl_lib, b"eglCreatePbufferSurface\0".as_ptr() as *const c_char));
        let egl_make_curr: Option<FnMakeCurrent> = std::mem::transmute(libc::dlsym(egl_lib, b"eglMakeCurrent\0".as_ptr() as *const c_char));
        let egl_destroy_surf: Option<FnDestroySurface> = std::mem::transmute(libc::dlsym(egl_lib, b"eglDestroySurface\0".as_ptr() as *const c_char));
        let egl_destroy_ctx: Option<FnDestroyContext> = std::mem::transmute(libc::dlsym(egl_lib, b"eglDestroyContext\0".as_ptr() as *const c_char));
        let egl_terminate: Option<FnTerminate> = std::mem::transmute(libc::dlsym(egl_lib, b"eglTerminate\0".as_ptr() as *const c_char));
        let gl_get_string: Option<FnGetString> = std::mem::transmute(libc::dlsym(gles_lib, b"glGetString\0".as_ptr() as *const c_char));

        if egl_get_display.is_none() || egl_init.is_none() || egl_choose_cfg.is_none() ||
           egl_create_ctx.is_none() || egl_create_pb.is_none() || egl_make_curr.is_none() ||
           gl_get_string.is_none() {
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"Missing core EGL/GLES entry points"}"#.to_string();
        }

        let egl_get_display = egl_get_display.unwrap();
        let egl_init = egl_init.unwrap();
        let egl_choose_cfg = egl_choose_cfg.unwrap();
        let egl_create_ctx = egl_create_ctx.unwrap();
        let egl_create_pb = egl_create_pb.unwrap();
        let egl_make_curr = egl_make_curr.unwrap();
        let egl_destroy_surf = egl_destroy_surf.unwrap();
        let egl_destroy_ctx = egl_destroy_ctx.unwrap();
        let egl_terminate = egl_terminate.unwrap();
        let gl_get_string = gl_get_string.unwrap();

        let dpy = egl_get_display(EGL_DEFAULT_DISPLAY);
        if dpy.is_null() {
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"eglGetDisplay returned null"}"#.to_string();
        }

        let mut maj = 0;
        let mut min = 0;
        if egl_init(dpy, &mut maj, &mut min) == 0 {
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"eglInitialize failed"}"#.to_string();
        }

        let config_attribs = [
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE,
        ];
        let mut cfg: EGLConfig = std::ptr::null_mut();
        let mut num_configs = 0;
        let mut chosen = egl_choose_cfg(dpy, config_attribs.as_ptr(), &mut cfg, 1, &mut num_configs);
        if chosen == 0 || num_configs == 0 {
            let config_attribs_fallback = [
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_NONE,
            ];
            chosen = egl_choose_cfg(dpy, config_attribs_fallback.as_ptr(), &mut cfg, 1, &mut num_configs);
        }

        if chosen == 0 || num_configs == 0 {
            egl_terminate(dpy);
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"No compatible EGL framebuffer configs found"}"#.to_string();
        }

        let pb_attribs = [EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE];
        let surface = egl_create_pb(dpy, cfg, pb_attribs.as_ptr());
        if surface == EGL_NO_SURFACE {
            egl_terminate(dpy);
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"Failed creating EGL test pbuffer surface"}"#.to_string();
        }

        let ctx_attribs_v3 = [EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE];
        let mut context = egl_create_ctx(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_v3.as_ptr());
        let mut is_v3 = true;
        if context == EGL_NO_CONTEXT {
            let ctx_attribs_v2 = [EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE];
            context = egl_create_ctx(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_v2.as_ptr());
            is_v3 = false;
        }

        if context == EGL_NO_CONTEXT {
            egl_destroy_surf(dpy, surface);
            egl_terminate(dpy);
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"Failed creating GLES 3.x/2.x context"}"#.to_string();
        }

        if egl_make_curr(dpy, surface, surface, context) == 0 {
            egl_destroy_ctx(dpy, context);
            egl_destroy_surf(dpy, surface);
            egl_terminate(dpy);
            libc::dlclose(gles_lib);
            libc::dlclose(egl_lib);
            return r#"{"backend":"opengl","api":"OpenGL ES","driverLoadFailed":true,"probeError":"eglMakeCurrent failed on test surface"}"#.to_string();
        }

        let renderer_raw = gl_get_string(GL_RENDERER);
        let renderer = if !renderer_raw.is_null() {
            CStr::from_ptr(renderer_raw).to_string_lossy().into_owned()
        } else {
            "Unknown OpenGL ES GPU".to_string()
        };

        let vendor_raw = gl_get_string(GL_VENDOR);
        let vendor = if !vendor_raw.is_null() {
            CStr::from_ptr(vendor_raw).to_string_lossy().into_owned()
        } else {
            "Unknown Vendor".to_string()
        };

        let version_raw = gl_get_string(GL_VERSION);
        let version = if !version_raw.is_null() {
            CStr::from_ptr(version_raw).to_string_lossy().into_owned()
        } else {
            if is_v3 { "OpenGL ES 3.0".to_string() } else { "OpenGL ES 2.0".to_string() }
        };

        let extensions_raw = gl_get_string(GL_EXTENSIONS);
        let extensions = if !extensions_raw.is_null() {
            CStr::from_ptr(extensions_raw).to_string_lossy().into_owned()
        } else {
            String::new()
        };

        let has_bc = extensions.contains("GL_EXT_texture_compression_s3tc")
            || extensions.contains("GL_NV_texture_compression_s3tc")
            || extensions.contains("GL_EXT_texture_compression_dxt1");

        let gles_short = if version.contains("3.2") {
            "3.2"
        } else if version.contains("3.1") {
            "3.1"
        } else if version.contains("3.0") || is_v3 {
            "3.0"
        } else {
            "2.0"
        };

        let driver_name = if vendor.contains("Qualcomm") || renderer.contains("Adreno") {
            "Qualcomm Adreno GLES Driver"
        } else if vendor.contains("ARM") || renderer.contains("Mali") || renderer.contains("Immortalis") {
            "ARM Mali Driver"
        } else if vendor.contains("Imagination") || renderer.contains("PowerVR") {
            "Imagination PowerVR Rogue Driver"
        } else if vendor.contains("Samsung") || renderer.contains("Xclipse") {
            "Samsung Xclipse RDNA Driver"
        } else {
            "OpenGL ES Native Driver"
        };

        // Teardown
        egl_make_curr(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        egl_destroy_ctx(dpy, context);
        egl_destroy_surf(dpy, surface);
        egl_terminate(dpy);
        libc::dlclose(gles_lib);
        libc::dlclose(egl_lib);

        format!(
            r#"{{"backend":"opengl","api":"OpenGL ES","gles":"{}","gpu":"{}","vendorId":0,"driverName":"{}","driverInfo":"{}","bcTextures":{},"logicalDeviceResult":0,"driverLoadFailed":false,"compatible":true,"missing":[]}}"#,
            escape_json(gles_short),
            escape_json(&renderer),
            escape_json(driver_name),
            escape_json(&version),
            has_bc
        )
    }
}

// ---- C ABI Exports ---------------------------------------------------------------------------------

/// Probes OpenGL ES and copies JSON diagnostic report into output buffer.
#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_probe_gles(out_buf: *mut c_char, max_len: usize) -> c_int {
    if out_buf.is_null() || max_len == 0 {
        return -1;
    }
    let report = probe_gles();
    let c_report = match CString::new(report) {
        Ok(c) => c,
        Err(_) => return -1,
    };
    libc::strncpy(out_buf, c_report.as_ptr(), max_len - 1);
    0
}

// ---- JNI Exports for io.mininxd.nfsu2.GpuProbeActivity ----------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_GpuProbeActivity_nativeProbeGles(
    env: jni::JNIEnv,
    _class: jni::objects::JClass,
) -> jni::sys::jstring {
    let report = probe_gles();
    let c_report = match CString::new(report) {
        Ok(c) => c,
        Err(_) => return std::ptr::null_mut(),
    };
    (**env.get_raw()).NewStringUTF.unwrap()(env.get_raw(), c_report.as_ptr())
}
