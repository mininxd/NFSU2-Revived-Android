//! Universal Vulkan Driver Loader in pure Rust.
//! Replaces android/app/src/main/cpp/vulkan_driver.cpp with universal multi-GPU support:
//! - Standard system Vulkan (`libvulkan.so`) across all SoCs:
//!   - ARM Mali (MediaTek Dimensity / Helio)
//!   - Qualcomm Adreno
//!   - Samsung Exynos / Xclipse (AMD RDNA)
//!   - Google Tensor
//!   - PowerVR Rogue / BXM
//! - Custom Vulkan driver packages / ICDs (Turnip, custom Mesa, etc.) when specified.

use std::ffi::{CStr, c_void};
use std::os::raw::c_char;

extern "C" {
    fn universal_gpu_init(
        hooks: *const c_char,
        temp: *const c_char,
        directory: *const c_char,
        library: *const c_char,
    ) -> i32;
    fn universal_gpu_get_handle() -> *mut c_void;
    fn universal_gpu_get_proc_addr() -> *mut c_void;
}

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
    let ok = universal_gpu_init(hooks, temp, directory, library);
    if ok != 0 {
        universal_gpu_get_handle()
    } else {
        std::ptr::null_mut()
    }
}

/// Returns the vkGetInstanceProcAddr function pointer.
pub unsafe fn get_vulkan_entry(handle: *mut c_void) -> *mut c_void {
    if handle.is_null() {
        std::ptr::null_mut()
    } else {
        universal_gpu_get_proc_addr()
    }
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
