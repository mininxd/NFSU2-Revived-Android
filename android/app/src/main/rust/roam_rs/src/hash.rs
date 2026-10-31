//! FNV-1a hash and string normalization.

use std::os::raw::c_char;
use std::ffi::CStr;

pub const RC_NAME_MAX: usize = 24;

#[no_mangle]
pub extern "C" fn rc_fnv1a(s: *const c_char) -> u32 {
    if s.is_null() {
        return 0;
    }
    let cstr = unsafe { CStr::from_ptr(s) };
    let bytes = cstr.to_bytes();
    if bytes.is_empty() {
        return 0;
    }
    let mut h: u32 = 0x811C_9DC5;
    for &b in bytes {
        h ^= b as u32;
        h = h.wrapping_mul(0x0100_0193);
    }
    if h == 0 { 1 } else { h }
}

#[no_mangle]
pub extern "C" fn rc_fnv1a_bytes(p: *const u8, n: u32) -> u32 {
    if p.is_null() || n == 0 {
        return 1;
    }
    let slice = unsafe { std::slice::from_raw_parts(p, n as usize) };
    let mut h: u32 = 0x811C_9DC5;
    for &b in slice {
        h ^= b as u32;
        h = h.wrapping_mul(0x0100_0193);
    }
    if h == 0 { 1 } else { h }
}

#[no_mangle]
pub extern "C" fn rc_room_hash(s: *const c_char) -> u32 {
    if s.is_null() {
        return 0;
    }
    let cstr = unsafe { CStr::from_ptr(s) };
    let bytes = cstr.to_bytes();
    let mut h: u32 = 0x811C_9DC5;
    let mut count = 0;

    for &raw_b in bytes {
        if raw_b == b' ' || (raw_b >= b'\t' && raw_b <= b'\r') {
            continue;
        }
        let b = if raw_b >= b'A' && raw_b <= b'Z' {
            raw_b + (b'a' - b'A')
        } else {
            raw_b
        };
        h ^= b as u32;
        h = h.wrapping_mul(0x0100_0193);
        count += 1;
    }

    if count == 0 {
        0
    } else if h == 0 {
        1
    } else {
        h
    }
}

#[no_mangle]
pub extern "C" fn rc_name_clean(dst: *mut c_char, src: *const c_char, srclen: u32) {
    if dst.is_null() {
        return;
    }
    let mut out_len = 0;
    if !src.is_null() && srclen > 0 {
        let src_bytes = unsafe { std::slice::from_raw_parts(src as *const u8, srclen as usize) };
        for &b in src_bytes {
            if b == 0 || out_len >= RC_NAME_MAX {
                break;
            }
            if b == b' ' && out_len == 0 {
                continue;
            }
            let valid = b >= 0x20 && b < 0x7F;
            let ch = if valid { b } else { b'?' };
            unsafe {
                *dst.add(out_len) = ch as c_char;
            }
            out_len += 1;
        }
    }

    while out_len > 0 {
        let prev = unsafe { *dst.add(out_len - 1) as u8 };
        if prev == b' ' {
            out_len -= 1;
        } else {
            break;
        }
    }

    if out_len == 0 {
        let default_name = b"Player\0";
        unsafe {
            std::ptr::copy_nonoverlapping(default_name.as_ptr() as *const c_char, dst, default_name.len());
        }
    } else {
        unsafe {
            *dst.add(out_len) = 0;
        }
    }
}
