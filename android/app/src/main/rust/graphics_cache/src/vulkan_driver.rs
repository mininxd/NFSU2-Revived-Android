//! Safe Rust interface for Vulkan Driver & Universal GPU Loader.
//! Replaces android/app/src/main/cpp/vulkan_driver.cpp.

use std::ffi::c_void;
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

/// Opens the specified Vulkan driver (system or AdrenoTools package).
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
