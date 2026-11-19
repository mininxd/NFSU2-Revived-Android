//! Categorized logging engine replacing src/platform/nfs_log.c with memory safety and zero data races.

use std::ffi::{CStr, CString};
use std::fs::{File, OpenOptions};
use std::io::{self, Write};
use std::os::raw::{c_char, c_int};
use std::sync::Mutex;
use std::time::SystemTime;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogCategory {
    Boot = 0,
    Cpu = 1,
    Icall = 2,
    Import = 3,
    Win32 = 4,
    D3d9 = 5,
    Thread = 6,
    File = 7,
    Audio = 8,
    Crash = 9,
}

impl LogCategory {
    pub const COUNT: usize = 10;

    #[inline]
    pub fn name(self) -> &'static str {
        match self {
            LogCategory::Boot => "BOOT",
            LogCategory::Cpu => "CPU",
            LogCategory::Icall => "ICALL",
            LogCategory::Import => "IMPORT",
            LogCategory::Win32 => "WIN32",
            LogCategory::D3d9 => "D3D9",
            LogCategory::Thread => "THREAD",
            LogCategory::File => "FILE",
            LogCategory::Audio => "AUDIO",
            LogCategory::Crash => "CRASH",
        }
    }

    #[inline]
    pub fn from_i32(val: i32) -> Self {
        match val {
            0 => LogCategory::Boot,
            1 => LogCategory::Cpu,
            2 => LogCategory::Icall,
            3 => LogCategory::Import,
            4 => LogCategory::Win32,
            5 => LogCategory::D3d9,
            6 => LogCategory::Thread,
            7 => LogCategory::File,
            8 => LogCategory::Audio,
            _ => LogCategory::Crash,
        }
    }
}

struct LoggerState {
    dir: String,
    stamp: String,
    dir_c: CString,
    stamp_c: CString,
    file: Option<File>,
}

static LOGGER: Mutex<Option<LoggerState>> = Mutex::new(None);

fn generate_timestamp() -> String {
    let now = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    // Simple fast UTC-like date format without heavy chrono dependency
    format!("{:010}", now)
}

pub fn init_logger(log_dir: Option<&str>) {
    let stamp = generate_timestamp();
    let dir = log_dir.unwrap_or(".").to_string();
    let stamp_c = CString::new(stamp.as_str()).unwrap_or_else(|_| CString::new("nostamp").unwrap());
    let dir_c = CString::new(dir.as_str()).unwrap_or_else(|_| CString::new(".").unwrap());

    let file = if log_dir.is_some() {
        let path = format!("{}/run_{}.log", dir, stamp);
        OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&path)
            .ok()
    } else {
        None
    };

    let mut lock = LOGGER.lock().unwrap();
    *lock = Some(LoggerState {
        dir,
        stamp,
        dir_c,
        stamp_c,
        file,
    });
}

pub fn write_log(cat: LogCategory, message: &str) {
    let header = format!("[NFSU2:{}] ", cat.name());
    let mut lock = LOGGER.lock().unwrap();

    // Print to stderr
    let _ = io::stderr().write_all(header.as_bytes());
    let _ = io::stderr().write_all(message.as_bytes());
    let _ = io::stderr().write_all(b"\n");

    // Output to log file if open
    if let Some(state) = lock.as_mut() {
        if let Some(ref mut f) = state.file {
            let _ = f.write_all(header.as_bytes());
            let _ = f.write_all(message.as_bytes());
            let _ = f.write_all(b"\n");
        }
    }
}

pub fn flush_logger() {
    let _ = io::stderr().flush();
    let mut lock = LOGGER.lock().unwrap();
    if let Some(state) = lock.as_mut() {
        if let Some(ref mut f) = state.file {
            let _ = f.flush();
        }
    }
}

// ---- C ABI Exports --------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_log_init(log_dir: *const c_char) {
    let dir_str = if !log_dir.is_null() {
        CStr::from_ptr(log_dir).to_str().ok()
    } else {
        None
    };
    init_logger(dir_str);
}

#[no_mangle]
pub extern "C" fn nfs_log_dir() -> *const c_char {
    let lock = LOGGER.lock().unwrap();
    if let Some(state) = lock.as_ref() {
        state.dir_c.as_ptr()
    } else {
        b".\0".as_ptr() as *const c_char
    }
}

#[no_mangle]
pub extern "C" fn nfs_log_stamp() -> *const c_char {
    let lock = LOGGER.lock().unwrap();
    if let Some(state) = lock.as_ref() {
        state.stamp_c.as_ptr()
    } else {
        b"nostamp\0".as_ptr() as *const c_char
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_log_write_c(cat: c_int, msg: *const c_char) {
    if msg.is_null() {
        return;
    }
    if let Ok(s) = CStr::from_ptr(msg).to_str() {
        write_log(LogCategory::from_i32(cat), s);
    }
}

#[no_mangle]
pub extern "C" fn nfs_log_flush() {
    flush_logger();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_category_names() {
        assert_eq!(LogCategory::Boot.name(), "BOOT");
        assert_eq!(LogCategory::Cpu.name(), "CPU");
        assert_eq!(LogCategory::Crash.name(), "CRASH");
    }

    #[test]
    fn test_init_and_flush() {
        init_logger(None);
        write_log(LogCategory::Boot, "Testing Rust platform logger");
        flush_logger();
    }
}
