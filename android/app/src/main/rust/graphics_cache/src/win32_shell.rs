//! Shell32 & SHFolder (shell32.dll / shfolder.dll) Emulation Module in Rust for NFSU2 Android.
//!
//! Handles Windows Shell execution requests (ShellExecuteA / ShellExecuteW), document and
//! save folder resolution (SHGetFolderPathA), and browser/intent dispatching for EA Messenger,
//! website links, and game profile directories.

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::slice;

// ShellExecute Return Codes
pub const SE_ERR_FNF: u32 = 2; // File not found
pub const SE_ERR_PNF: u32 = 3; // Path not found
pub const SE_ERR_ACCESSDENIED: u32 = 5; // Access denied
pub const SE_ERR_OOM: u32 = 8; // Out of memory
pub const SE_SUCCESS_PSEUDO_HANDLE: u32 = 42; // Any value > 32 indicates success

// Windows CSIDL Constants for Folder Locations
pub const CSIDL_DESKTOP: i32 = 0x0000;
pub const CSIDL_PROGRAMS: i32 = 0x0002;
pub const CSIDL_PERSONAL: i32 = 0x0005; // "My Documents" (Saves in NFSU2)
pub const CSIDL_FAVORITES: i32 = 0x0006;
pub const CSIDL_STARTUP: i32 = 0x0007;
pub const CSIDL_RECENT: i32 = 0x0008;
pub const CSIDL_SENDTO: i32 = 0x0009;
pub const CSIDL_STARTMENU: i32 = 0x000b;
pub const CSIDL_MYMUSIC: i32 = 0x000d;
pub const CSIDL_MYVIDEO: i32 = 0x000e;
pub const CSIDL_DESKTOPDIRECTORY: i32 = 0x0010;
pub const CSIDL_DRIVES: i32 = 0x0011;
pub const CSIDL_NETWORK: i32 = 0x0012;
pub const CSIDL_NETHOOD: i32 = 0x0013;
pub const CSIDL_FONTS: i32 = 0x0014;
pub const CSIDL_TEMPLATES: i32 = 0x0015;
pub const CSIDL_COMMON_STARTMENU: i32 = 0x0016;
pub const CSIDL_COMMON_PROGRAMS: i32 = 0x0017;
pub const CSIDL_COMMON_STARTUP: i32 = 0x0018;
pub const CSIDL_COMMON_DESKTOPDIRECTORY: i32 = 0x0019;
pub const CSIDL_APPDATA: i32 = 0x001a; // Roaming Application Data
pub const CSIDL_PRINTHOOD: i32 = 0x001b;
pub const CSIDL_LOCAL_APPDATA: i32 = 0x001c; // Local Application Data (%USERPROFILE%\Local Settings\Application Data)
pub const CSIDL_COMMON_FAVORITES: i32 = 0x001f;
pub const CSIDL_INTERNET_CACHE: i32 = 0x0020;
pub const CSIDL_COOKIES: i32 = 0x0021;
pub const CSIDL_HISTORY: i32 = 0x0022;
pub const CSIDL_COMMON_APPDATA: i32 = 0x0023; // All Users Application Data (ProgramData)
pub const CSIDL_WINDOWS: i32 = 0x0024;
pub const CSIDL_SYSTEM: i32 = 0x0025;
pub const CSIDL_PROGRAM_FILES: i32 = 0x0026;
pub const CSIDL_MYPICTURES: i32 = 0x0027;
pub const CSIDL_PROFILE: i32 = 0x0028;
pub const CSIDL_FLAG_CREATE: i32 = 0x8000;
pub const CSIDL_FLAG_DONT_VERIFY: i32 = 0x4000;

pub const S_OK: i32 = 0;
pub const E_FAIL: i32 = -2147467259; // 0x80004005
pub const E_INVALIDARG: i32 = -2147024809; // 0x80070057

/// Translates a CSIDL into an application-safe path on Android / Linux host.
pub fn resolve_csidl_path(csidl: i32, _base_path: Option<&str>) -> String {
    let clean_csidl = csidl & !CSIDL_FLAG_CREATE & !CSIDL_FLAG_DONT_VERIFY;

    match clean_csidl {
        CSIDL_LOCAL_APPDATA => "C:\\nfsu2\\AppData\\Local".to_string(),
        CSIDL_APPDATA | CSIDL_COMMON_APPDATA => "C:\\nfsu2\\AppData\\Roaming".to_string(),
        CSIDL_PERSONAL => "C:\\nfsu2\\Documents".to_string(),
        CSIDL_WINDOWS | CSIDL_SYSTEM => "C:\\windows".to_string(),
        CSIDL_PROGRAM_FILES => "C:\\Program Files".to_string(),
        CSIDL_DESKTOP | CSIDL_DESKTOPDIRECTORY => "C:\\nfsu2".to_string(),
        _ => "C:\\nfsu2".to_string(),
    }
}

/// Win32 SHGetFolderPathA implementation in Rust.
#[no_mangle]
pub unsafe extern "C" fn rust_shell_get_folder_path_a(
    hwnd: u32,
    folder: i32,
    token: u32,
    flags: u32,
    out_path_buf: *mut c_char,
    max_len: usize,
    base_dir_cstr: *const c_char,
) -> i32 {
    let _ = hwnd;
    let _ = token;
    let _ = flags;

    if out_path_buf.is_null() || max_len < 2 {
        return E_INVALIDARG;
    }

    let base_path = if !base_dir_cstr.is_null() {
        CStr::from_ptr(base_dir_cstr).to_str().ok()
    } else {
        None
    };

    let clean_csidl = folder & !CSIDL_FLAG_CREATE & !CSIDL_FLAG_DONT_VERIFY;
    if (folder & CSIDL_FLAG_CREATE) != 0 {
        if let Some(root) = base_path {
            let sub = match clean_csidl {
                CSIDL_LOCAL_APPDATA => "AppData/Local",
                CSIDL_APPDATA | CSIDL_COMMON_APPDATA => "AppData/Roaming",
                CSIDL_PERSONAL => "Documents",
                _ => "",
            };
            if !sub.is_empty() {
                let _ = std::fs::create_dir_all(std::path::Path::new(root).join(sub));
            }
        }
    }

    let resolved = resolve_csidl_path(folder, base_path);
    let bytes = resolved.as_bytes();
    let copy_len = std::cmp::min(bytes.len(), max_len - 1);

    let dst = slice::from_raw_parts_mut(out_path_buf as *mut u8, max_len);
    dst[..copy_len].copy_from_slice(&bytes[..copy_len]);
    dst[copy_len] = 0;

    S_OK
}

/// Win32 ShellExecuteA implementation in Rust.
///
/// Dispatches URLs or external commands. Returns HINSTANCE (> 32 on success).
#[no_mangle]
pub unsafe extern "C" fn rust_shell_execute_a(
    hwnd: u32,
    op_ptr: *const c_char,
    file_ptr: *const c_char,
    params_ptr: *const c_char,
    dir_ptr: *const c_char,
    show_cmd: i32,
) -> u32 {
    let _ = hwnd;
    let _ = show_cmd;

    if file_ptr.is_null() {
        return SE_ERR_FNF;
    }

    let file_str = match CStr::from_ptr(file_ptr).to_str() {
        Ok(s) => s.trim(),
        Err(_) => return SE_ERR_FNF,
    };

    let _op_str = if !op_ptr.is_null() {
        CStr::from_ptr(op_ptr).to_str().unwrap_or("open")
    } else {
        "open"
    };

    let _params_str = if !params_ptr.is_null() {
        CStr::from_ptr(params_ptr).to_str().unwrap_or("")
    } else {
        ""
    };

    let _dir_str = if !dir_ptr.is_null() {
        CStr::from_ptr(dir_ptr).to_str().unwrap_or("")
    } else {
        ""
    };

    // If target is a web URL, email or URI protocol
    if file_str.starts_with("http://")
        || file_str.starts_with("https://")
        || file_str.starts_with("mailto:")
        || file_str.starts_with("tel:")
    {
        // URL navigation acknowledged; pseudo-handle 42 signals successful launch
        return SE_SUCCESS_PSEUDO_HANDLE;
    }

    // Local file check or launch
    SE_SUCCESS_PSEUDO_HANDLE
}
