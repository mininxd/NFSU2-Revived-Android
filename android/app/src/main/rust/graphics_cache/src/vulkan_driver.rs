//! Universal Vulkan Driver Loader in pure Rust.
//! Replaces android/app/src/main/cpp/vulkan_driver.cpp with universal multi-GPU support:
//! - Standard system Vulkan (`libvulkan.so`) across all SoCs:
//!   - ARM Mali (MediaTek Dimensity / Helio)
//!   - Qualcomm Adreno
//!   - Samsung Exynos / Xclipse (AMD RDNA)
//!   - Google Tensor
//!   - PowerVR Rogue / BXM
//! - Custom Vulkan driver packages / ICDs (Turnip, custom Mesa, etc.) when specified.

use std::ffi::c_void;
use std::os::raw::c_char;
use std::sync::Once;

type FnUniversalGpuInit = unsafe extern "C" fn(
    *const c_char,
    *const c_char,
    *const c_char,
    *const c_char,
) -> i32;
type FnUniversalGpuGetPtr = unsafe extern "C" fn() -> *mut c_void;

static RESOLVE_ONCE: Once = Once::new();
static mut FN_INIT: Option<FnUniversalGpuInit> = None;
static mut FN_GET_HANDLE: Option<FnUniversalGpuGetPtr> = None;
static mut FN_GET_PROC_ADDR: Option<FnUniversalGpuGetPtr> = None;

unsafe fn resolve_universal_gpu() {
    RESOLVE_ONCE.call_once(|| {
        let mut handle = libc::RTLD_DEFAULT;
        let init_sym = libc::dlsym(handle, b"universal_gpu_init\0".as_ptr() as *const c_char);
        if init_sym.is_null() {
            let diag = libc::dlopen(
                b"libnfsu2_diagnostics.so\0".as_ptr() as *const c_char,
                libc::RTLD_NOLOAD | libc::RTLD_GLOBAL,
            );
            if !diag.is_null() {
                handle = diag;
            }
        }
        let s_init = libc::dlsym(handle, b"universal_gpu_init\0".as_ptr() as *const c_char);
        let s_handle = libc::dlsym(handle, b"universal_gpu_get_handle\0".as_ptr() as *const c_char);
        let s_proc = libc::dlsym(handle, b"universal_gpu_get_proc_addr\0".as_ptr() as *const c_char);
        if !s_init.is_null() && !s_handle.is_null() && !s_proc.is_null() {
            FN_INIT = Some(std::mem::transmute(s_init));
            FN_GET_HANDLE = Some(std::mem::transmute(s_handle));
            FN_GET_PROC_ADDR = Some(std::mem::transmute(s_proc));
        }
    });
}

static mut FALLBACK_VK_HANDLE: *mut c_void = std::ptr::null_mut();

/// Checks if the selected driver request is for the universal system driver.
pub fn is_system_driver(library: Option<&str>) -> bool {
    match library {
        None => true,
        Some(s) => s.is_empty() || s.eq_ignore_ascii_case("system"),
    }
}

/// Universally opens the Vulkan driver for any Android GPU.
/// If `library` is NULL or "system", loads system `libvulkan.so` for Mali, Adreno, Immortalis, etc.
/// If `library` points to a custom package, initializes through the custom driver namespace.
pub unsafe fn open_vulkan(
    hooks: *const c_char,
    temp: *const c_char,
    directory: *const c_char,
    library: *const c_char,
) -> *mut c_void {
    resolve_universal_gpu();
    if let (Some(init), Some(get_handle)) = (FN_INIT, FN_GET_HANDLE) {
        let ok = init(hooks, temp, directory, library);
        return if ok != 0 {
            get_handle()
        } else {
            std::ptr::null_mut()
        };
    }

    if !FALLBACK_VK_HANDLE.is_null() {
        return FALLBACK_VK_HANDLE;
    }
    let h = libc::dlopen(
        b"libvulkan.so\0".as_ptr() as *const c_char,
        libc::RTLD_NOW | libc::RTLD_LOCAL,
    );
    FALLBACK_VK_HANDLE = h;
    h
}

/// Returns the vkGetInstanceProcAddr function pointer.
pub unsafe fn get_vulkan_entry(handle: *mut c_void) -> *mut c_void {
    if handle.is_null() {
        return std::ptr::null_mut();
    }
    resolve_universal_gpu();
    if let Some(get_proc) = FN_GET_PROC_ADDR {
        return get_proc();
    }
    libc::dlsym(
        handle,
        b"vkGetInstanceProcAddr\0".as_ptr() as *const c_char,
    )
}

// ---------------------------------------------------------------------------
// C ABI Compatibility Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn rust_nfs_open_vulkan(
    hooks: *const c_char,
    temp: *const c_char,
    directory: *const c_char,
    library: *const c_char,
) -> *mut c_void {
    open_vulkan(hooks, temp, directory, library)
}

#[no_mangle]
pub unsafe extern "C" fn rust_nfs_vulkan_entry(handle: *mut c_void) -> *mut c_void {
    get_vulkan_entry(handle)
}
