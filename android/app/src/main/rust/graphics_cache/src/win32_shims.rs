//! Safe Rust implementation of Win32 guest shims and PE resource traversal for NFSU2.
//! Replaces src/shims/win32_shims.c with memory safety and bounds checking.

use std::ffi::CStr;
use std::os::raw::c_char;

pub const NFS_GUEST_BASE: u32 = 0x00400000;

// Win32 Window Styles
pub const GWL_STYLE: i32 = -16;
pub const GWL_EXSTYLE: i32 = -20;
pub const WS_OVERLAPPEDWINDOW: u32 = 0x00CF0000;
pub const WS_THICKFRAME: u32 = 0x00040000;
pub const WS_MAXIMIZEBOX: u32 = 0x00010000;
pub const WS_VISIBLE: u32 = 0x10000000;
pub const WS_EX_TOPMOST: u32 = 0x00000008;

/// Checks if a module name refers to the guest executable (SPEED2.EXE).
pub fn is_guest_name(s: &str) -> bool {
    let name = s.rsplit(|c| c == '\\' || c == '/').next().unwrap_or(s);
    name.eq_ignore_ascii_case("SPEED2.EXE") || name.eq_ignore_ascii_case("SPEED2")
}

/// Shims GetModuleHandleA for the guest executable.
pub unsafe fn get_module_handle_a(name_ptr: *const c_char) -> u32 {
    if name_ptr.is_null() {
        return NFS_GUEST_BASE;
    }
    if let Ok(name_str) = CStr::from_ptr(name_ptr).to_str() {
        if is_guest_name(name_str) {
            return NFS_GUEST_BASE;
        }
    }
    0
}

/// Shims GetModuleFileNameA for the guest executable.
pub unsafe fn get_module_file_name_a(
    m: u32,
    buf: *mut c_char,
    n: u32,
    guest_exe_path: &str,
) -> u32 {
    if buf.is_null() || n == 0 {
        return 0;
    }
    if m == 0 || m == NFS_GUEST_BASE {
        let bytes = guest_exe_path.as_bytes();
        let max_copy = (n - 1) as usize;
        let copy_len = bytes.len().min(max_copy);
        std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, copy_len);
        *buf.add(copy_len) = 0;
        copy_len as u32
    } else {
        0
    }
}

/// Modifies window styles for windowed mode.
pub fn filter_window_long(index: i32, value: u32, windowed: bool, is_game_window: bool) -> u32 {
    if windowed && is_game_window {
        if index == GWL_STYLE {
            let base = WS_OVERLAPPEDWINDOW & !(WS_THICKFRAME | WS_MAXIMIZEBOX);
            return base | (value & WS_VISIBLE);
        } else if index == GWL_EXSTYLE {
            return value & !WS_EX_TOPMOST;
        }
    }
    value
}

// ---------------------------------------------------------------------------
// PE Resource Traversal for Guest RT_RCDATA (D3DX Shaders & Effects)
// ---------------------------------------------------------------------------

#[repr(C, packed)]
struct ImageResourceDirectory {
    characteristics: u32,
    time_date_stamp: u32,
    major_version: u16,
    minor_version: u16,
    number_of_named_entries: u16,
    number_of_id_entries: u16,
}

#[repr(C, packed)]
struct ImageResourceDirectoryEntry {
    name_or_id: u32,
    offset_to_data: u32,
}

#[repr(C, packed)]
struct ImageResourceDataEntry {
    offset_to_data: u32,
    size: u32,
    code_page: u32,
    reserved: u32,
}

pub unsafe fn guest_find_resource(guest_base: usize, name: u32, rtype: u32) -> u32 {
    if guest_base == 0 {
        return 0;
    }
    // Read DOS Header e_lfanew at guest_base + 0x3C
    let e_lfanew = *( (guest_base + 0x3C) as *const u32 ) as usize;
    if e_lfanew == 0 || e_lfanew > 0x1000 {
        return 0;
    }
    // Resource DataDirectory is index 2 (offset in OptionalHeader is 0x60 + 2*8 = 0x70)
    let nt_headers = guest_base + e_lfanew;
    // Magic check: PE signature at nt_headers
    if *(nt_headers as *const u32) != 0x00004550 {
        return 0;
    }
    let rsrc_va = *( (nt_headers + 0x18 + 0x60 + 2 * 8) as *const u32 ) as usize;
    if rsrc_va == 0 {
        return 0;
    }
    let root = (guest_base + rsrc_va) as *const ImageResourceDirectory;
    let num_types = ((*root).number_of_named_entries + (*root).number_of_id_entries) as usize;
    let entries = root.add(1) as *const ImageResourceDirectoryEntry;

    // 1. Search for Type (e.g. RT_RCDATA = 10)
    let mut type_dir_offset: Option<usize> = None;
    for i in 0..num_types {
        let e = &*entries.add(i);
        if (e.offset_to_data & 0x80000000) != 0 && (e.name_or_id & 0xFFFF) == (rtype & 0xFFFF) {
            type_dir_offset = Some((e.offset_to_data & 0x7FFFFFFF) as usize);
            break;
        }
    }

    let t_offset = match type_dir_offset {
        Some(o) => o,
        None => return 0,
    };

    // 2. Search for Name/ID
    let type_dir = (guest_base + rsrc_va + t_offset) as *const ImageResourceDirectory;
    let num_names = ((*type_dir).number_of_named_entries + (*type_dir).number_of_id_entries) as usize;
    let name_entries = type_dir.add(1) as *const ImageResourceDirectoryEntry;

    let mut name_dir_offset: Option<usize> = None;
    for i in 0..num_names {
        let e = &*name_entries.add(i);
        if (e.offset_to_data & 0x80000000) != 0 && (e.name_or_id & 0xFFFF) == (name & 0xFFFF) {
            name_dir_offset = Some((e.offset_to_data & 0x7FFFFFFF) as usize);
            break;
        }
    }

    let n_offset = match name_dir_offset {
        Some(o) => o,
        None => return 0,
    };

    // 3. First Language
    let lang_dir = (guest_base + rsrc_va + n_offset) as *const ImageResourceDirectory;
    let lang_entries = lang_dir.add(1) as *const ImageResourceDirectoryEntry;
    let lang = &*lang_entries;
    if (lang.offset_to_data & 0x80000000) != 0 {
        return 0;
    }

    // Returns pointer to ImageResourceDataEntry
    (guest_base + rsrc_va + (lang.offset_to_data as usize)) as u32
}

pub unsafe fn guest_load_resource(guest_base: usize, hrsrc: u32) -> u32 {
    if hrsrc == 0 {
        return 0;
    }
    let data_entry = hrsrc as *const ImageResourceDataEntry;
    (guest_base as u32) + (*data_entry).offset_to_data
}

pub unsafe fn guest_sizeof_resource(hrsrc: u32) -> u32 {
    if hrsrc == 0 {
        return 0;
    }
    let data_entry = hrsrc as *const ImageResourceDataEntry;
    (*data_entry).size
}

// ---------------------------------------------------------------------------
// C ABI Compatibility Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn rust_shim_get_module_handle_a(name: *const c_char) -> u32 {
    get_module_handle_a(name)
}

#[no_mangle]
pub unsafe extern "C" fn rust_shim_get_module_file_name_a(
    m: u32,
    buf: *mut c_char,
    n: u32,
    path: *const c_char,
) -> u32 {
    let p = if !path.is_null() {
        CStr::from_ptr(path).to_str().unwrap_or("SPEED2.EXE")
    } else {
        "SPEED2.EXE"
    };
    get_module_file_name_a(m, buf, n, p)
}

#[no_mangle]
pub unsafe extern "C" fn rust_shim_find_resource(base: usize, name: u32, rtype: u32) -> u32 {
    guest_find_resource(base, name, rtype)
}

#[no_mangle]
pub unsafe extern "C" fn rust_shim_load_resource(base: usize, hrsrc: u32) -> u32 {
    guest_load_resource(base, hrsrc)
}

#[no_mangle]
pub unsafe extern "C" fn rust_shim_sizeof_resource(hrsrc: u32) -> u32 {
    guest_sizeof_resource(hrsrc)
}

#[no_mangle]
pub extern "C" fn rust_shim_filter_window_long(index: i32, value: u32, windowed: i32, is_game: i32) -> u32 {
    filter_window_long(index, value, windowed != 0, is_game != 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_guest_name() {
        assert!(is_guest_name("speed2.exe"));
        assert!(is_guest_name("SPEED2"));
        assert!(is_guest_name("C:\\Games\\NFSU2\\speed2.exe"));
        assert!(is_guest_name("/storage/emulated/0/NFSU2/SPEED2.EXE"));
        assert!(!is_guest_name("kernel32.dll"));
        assert!(!is_guest_name("d3d9.dll"));
    }

    #[test]
    fn test_filter_window_long() {
        // Fullscreen style: should be converted to framed window without topmost
        let original_style = WS_VISIBLE | 0x80000000;
        let filtered = filter_window_long(GWL_STYLE, original_style, true, true);
        assert_eq!(filtered & WS_VISIBLE, WS_VISIBLE);
        assert_eq!(filtered & (WS_THICKFRAME | WS_MAXIMIZEBOX), 0);

        let original_exstyle = WS_EX_TOPMOST | 0x00000040;
        let filtered_ex = filter_window_long(GWL_EXSTYLE, original_exstyle, true, true);
        assert_eq!(filtered_ex & WS_EX_TOPMOST, 0);
    }
}
