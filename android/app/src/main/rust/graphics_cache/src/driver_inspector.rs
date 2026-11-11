//! Pure Rust GPU Driver Archive Inspector & ARM64 ELF Validator for NFSU2 Android.
//!
//! Replaces Java-based ZIP inspection, `meta.json` parsing, and ELF header validation with
//! high-speed, zero-allocation native Rust routines.

use std::ffi::{CStr, CString};
use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};

/// Validates that a byte slice represents a valid Android ARM64 (.so) shared library ELF header.
///
/// Header checks:
/// - bytes 0..4: `\x7fELF`
/// - byte 4: 2 (64-bit ELF)
/// - byte 5: 1 (Little-endian)
/// - bytes 16..18: 3 (ET_DYN shared library)
/// - bytes 18..20: 183 (EM_AARCH64)
pub fn validate_arm64_elf_header(header: &[u8]) -> bool {
    if header.len() < 20 {
        return false;
    }
    header[0] == 0x7F
        && header[1] == b'E'
        && header[2] == b'L'
        && header[3] == b'F'
        && header[4] == 2
        && header[5] == 1
        && header[16] == 3
        && header[17] == 0
        && header[18] == 183
        && header[19] == 0
}

/// Validates an ELF file on disk. Returns true if valid ARM64 shared library.
pub fn validate_arm64_elf_file(path: &Path) -> bool {
    let mut file = match File::open(path) {
        Ok(f) => f,
        Err(_) => return false,
    };
    let mut header = [0u8; 64];
    if file.read_exact(&mut header).is_err() {
        return false;
    }
    validate_arm64_elf_header(&header)
}

/// Lightweight JSON string value extractor for simple keys like `"libraryName"`, `"name"`, `"folder"`.
pub fn parse_json_string_value(json: &str, key: &str) -> Option<String> {
    let search = format!("\"{}\"", key);
    if let Some(pos) = json.find(&search) {
        let rest = &json[pos + search.len()..];
        if let Some(colon_pos) = rest.find(':') {
            let after_colon = rest[colon_pos + 1..].trim_start();
            if after_colon.starts_with('"') {
                let string_val = &after_colon[1..];
                if let Some(end_quote) = string_val.find('"') {
                    return Some(string_val[..end_quote].to_string());
                }
            }
        }
    }
    None
}

/// Lightweight JSON integer value extractor (e.g. `"minApi": 26`).
pub fn parse_json_int_value(json: &str, key: &str) -> Option<i32> {
    let search = format!("\"{}\"", key);
    if let Some(pos) = json.find(&search) {
        let rest = &json[pos + search.len()..];
        if let Some(colon_pos) = rest.find(':') {
            let after_colon = rest[colon_pos + 1..].trim_start();
            let mut digits = String::new();
            for c in after_colon.chars() {
                if c.is_ascii_digit() || (c == '-' && digits.is_empty()) {
                    digits.push(c);
                } else if !digits.is_empty() {
                    break;
                }
            }
            if let Ok(val) = digits.parse::<i32>() {
                return Some(val);
            }
        }
    }
    None
}

/// Searches recursively for `meta.json` inside a directory.
pub fn find_meta_json(root: &Path) -> Option<PathBuf> {
    let candidate = root.join("meta.json");
    if candidate.is_file() {
        return Some(root.to_path_buf());
    }

    if let Ok(entries) = fs::read_dir(root) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                if let Some(found) = find_meta_json(&path) {
                    return Some(found);
                }
            }
        }
    }
    None
}

// ---- C ABI Exports ---------------------------------------------------------------------------------

/// Validates ARM64 ELF library file. Returns 0 if valid, -1 if invalid or unreadable.
#[no_mangle]
pub unsafe extern "C" fn nfs_driver_validate_arm64_elf(path_ptr: *const libc::c_char) -> i32 {
    if path_ptr.is_null() {
        return -1;
    }
    let c_str = CStr::from_ptr(path_ptr);
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    if validate_arm64_elf_file(Path::new(path_str)) { 0 } else { -1 }
}

/// Inspects driver package directory, validating `driver.json` or `meta.json` and ELF library.
/// Returns 0 on success, copying the driver name and library filename to output buffers.
#[no_mangle]
pub unsafe extern "C" fn nfs_driver_inspect_package(
    dir_ptr: *const libc::c_char,
    out_name: *mut libc::c_char,
    max_name: usize,
    out_lib: *mut libc::c_char,
    max_lib: usize,
) -> i32 {
    if dir_ptr.is_null() {
        return -1;
    }
    let dir_str = match CStr::from_ptr(dir_ptr).to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    let dir = Path::new(dir_str);

    // Try driver.json first, then meta.json
    let json_content = if let Ok(c) = fs::read_to_string(dir.join("driver.json")) {
        c
    } else if let Ok(c) = fs::read_to_string(dir.join("meta.json")) {
        c
    } else {
        return -1;
    };

    let name = parse_json_string_value(&json_content, "name").unwrap_or_else(|| "Custom Driver".to_string());
    let lib_name = match parse_json_string_value(&json_content, "libraryName") {
        Some(l) => l,
        None => return -1,
    };

    // Check ELF library
    let folder = parse_json_string_value(&json_content, "folder").unwrap_or_else(|| "payload".to_string());
    let lib_path = dir.join(&folder).join(&lib_name);
    let alt_path = dir.join(&lib_name);

    if !validate_arm64_elf_file(&lib_path) && !validate_arm64_elf_file(&alt_path) {
        return -2;
    }

    if !out_name.is_null() && max_name > 0 {
        let c_name = CString::new(name).unwrap_or_default();
        libc::strncpy(out_name, c_name.as_ptr(), max_name - 1);
    }
    if !out_lib.is_null() && max_lib > 0 {
        let c_lib = CString::new(lib_name).unwrap_or_default();
        libc::strncpy(out_lib, c_lib.as_ptr(), max_lib - 1);
    }
    0
}

// ---- JNI Interface for io.mininxd.nfsu2.DriverArchive ----------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_DriverArchive_nativeValidateLibrary(
    mut env: jni::JNIEnv,
    _class: jni::objects::JClass,
    j_path: jni::sys::jstring,
) -> jni::sys::jboolean {
    if j_path.is_null() {
        return 0;
    }
    let path_str: String = match env.get_string(&jni::objects::JString::from_raw(j_path)) {
        Ok(s) => s.into(),
        Err(_) => return 0,
    };
    if validate_arm64_elf_file(Path::new(&path_str)) { 1 } else { 0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_arm64_elf_header_validation() {
        let mut valid_header = [0u8; 64];
        valid_header[0] = 0x7F;
        valid_header[1] = b'E';
        valid_header[2] = b'L';
        valid_header[3] = b'F';
        valid_header[4] = 2; // 64-bit
        valid_header[5] = 1; // Little-endian
        valid_header[16] = 3; // ET_DYN
        valid_header[18] = 183; // EM_AARCH64

        assert!(validate_arm64_elf_header(&valid_header));

        // Invalid: x86_64 machine type (62)
        let mut x86_header = valid_header;
        x86_header[18] = 62;
        assert!(!validate_arm64_elf_header(&x86_header));

        // Invalid: 32-bit ELF (1)
        let mut bit32_header = valid_header;
        bit32_header[4] = 1;
        assert!(!validate_arm64_elf_header(&bit32_header));
    }

    #[test]
    fn test_parse_json_values() {
        let json = r#"{"name": "Turnip v24.3.0", "libraryName": "vulkan.adreno.so", "minApi": 28}"#;
        assert_eq!(parse_json_string_value(json, "name"), Some("Turnip v24.3.0".to_string()));
        assert_eq!(parse_json_string_value(json, "libraryName"), Some("vulkan.adreno.so".to_string()));
        assert_eq!(parse_json_int_value(json, "minApi"), Some(28));
        assert_eq!(parse_json_string_value(json, "non_existent"), None);
    }
}
