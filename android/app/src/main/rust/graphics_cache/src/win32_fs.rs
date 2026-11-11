//! High-Performance Case-Insensitive Filesystem Path Resolution Cache in Rust for NFSU2 Android.
//!
//! Eliminates storage I/O latency and repeated directory_iterator sweeps on Android flash storage
//! by caching resolved host paths in an O(1) concurrent memoization table.

use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::RwLock;

static PATH_CACHE: RwLock<Option<HashMap<String, String>>> = RwLock::new(None);

fn normalize_key(path: &str) -> String {
    let mut s = path.replace('\\', "/").to_ascii_lowercase();
    while s.starts_with('/') {
        s.remove(0);
    }
    if let Some(stripped) = s.strip_prefix("c:/nfsu2/") {
        stripped.to_string()
    } else if s == "c:/nfsu2" {
        String::new()
    } else {
        s
    }
}

/// Looks up a previously resolved host path for a guest path.
///
/// Returns 1 on cache hit (with resolved path copied into out_buf), or 0 on cache miss.
#[no_mangle]
pub unsafe extern "C" fn rust_fs_cache_lookup(
    guest_path: *const c_char,
    out_buf: *mut c_char,
    max_len: usize,
) -> c_int {
    if guest_path.is_null() || out_buf.is_null() || max_len == 0 {
        return 0;
    }

    let c_str = CStr::from_ptr(guest_path);
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return 0,
    };

    let key = normalize_key(path_str);

    let read_guard = match PATH_CACHE.read() {
        Ok(g) => g,
        Err(_) => return 0,
    };

    if let Some(map) = read_guard.as_ref() {
        if let Some(resolved) = map.get(&key) {
            let bytes = resolved.as_bytes();
            if bytes.len() + 1 <= max_len {
                std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, out_buf, bytes.len());
                *out_buf.add(bytes.len()) = 0;
                return 1;
            }
        }
    }

    0
}

/// Stores a newly resolved host path into the cache.
#[no_mangle]
pub unsafe extern "C" fn rust_fs_cache_insert(
    guest_path: *const c_char,
    host_path: *const c_char,
) {
    if guest_path.is_null() || host_path.is_null() {
        return;
    }

    let g_str = match CStr::from_ptr(guest_path).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };
    let h_str = match CStr::from_ptr(host_path).to_str() {
        Ok(s) => s,
        Err(_) => return,
    };

    let key = normalize_key(g_str);
    let value = h_str.to_string();

    let mut write_guard = match PATH_CACHE.write() {
        Ok(g) => g,
        Err(_) => return,
    };

    let map = write_guard.get_or_insert_with(HashMap::new);
    // Limit cache entries to 4096 to prevent unbounded memory growth on long play sessions
    if map.len() < 4096 {
        map.insert(key, value);
    }
}

/// Clears the path resolution cache.
#[no_mangle]
pub unsafe extern "C" fn rust_fs_cache_clear() {
    if let Ok(mut write_guard) = PATH_CACHE.write() {
        if let Some(map) = write_guard.as_mut() {
            map.clear();
        }
    }
}

/// Returns the current number of cached paths.
#[no_mangle]
pub unsafe extern "C" fn rust_fs_cache_size() -> usize {
    if let Ok(read_guard) = PATH_CACHE.read() {
        if let Some(map) = read_guard.as_ref() {
            return map.len();
        }
    }
    0
}
