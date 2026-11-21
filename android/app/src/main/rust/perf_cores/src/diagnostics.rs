//! Pure Rust Crash Log Filter & Technical Diagnostic Engine.
//! Eliminates regex pattern matching allocations in Java when generating diagnostic crash reports.

use jni::objects::{JClass, JString};
use jni::sys::jstring;
use jni::JNIEnv;

/// Key keywords indicating critical engine stalls, fatal errors, crashes, or unhandled shims.
pub const KEY_KEYWORDS: &[&str] = &[
    "stopped",
    "boundary",
    "pending",
    "pendiente",
    "unresolved",
    "stall",
    "  thread ",
    "FATAL",
    "Fatal",
    "SIGSEGV",
    "SIGBUS",
    "abort",
    "DeadSystem",
    "E DXVK",
    "MessageBox",
];

/// Noisy high-frequency logging lines that crowd out useful diagnostic summaries.
pub const NOISE_KEYWORDS: &[&str] = &[
    "Guest file ",
    "touch DIK",
    "Keyboard buffered",
    "D3D9 COM",
    "Guest timer",
    "Guest import",
    "Guest LoadLibrary d3d9",
    "VirtualProtect",
    "presentation",
];

/// Compacts an Android logcat threadtime line (e.g. `11-21 20:30:40.892 1234 5678 W Tag: message`)
/// into a compact `20:30:40.892 W message` format to save URL space in crash reports.
pub fn compact_threadtime_line(line: &str) -> String {
    let parts: Vec<&str> = line.split_whitespace().collect();
    if parts.len() < 6 {
        return line.trim().to_string();
    }

    let time = parts[1];
    let level = parts[4];
    // Reconstruct the message part (parts[5..])
    let rest = parts[5..].join(" ");
    let msg = if let Some(idx) = rest.find(": ") {
        &rest[idx + 2..]
    } else {
        &rest
    };

    format!("{} {} {}", time, level, msg)
}

pub fn is_key_line(line: &str) -> bool {
    for kw in KEY_KEYWORDS {
        if line.contains(kw) {
            return true;
        }
    }
    // Check for native crash stack frames: "#01 pc ... libnfsu2"
    if line.contains(" pc ") && line.contains("libnfsu2") && line.contains('#') {
        return true;
    }
    false
}

pub fn is_noise_line(line: &str) -> bool {
    for noise in NOISE_KEYWORDS {
        if line.contains(noise) {
            return true;
        }
    }
    false
}

#[derive(Debug, Clone, Default)]
pub struct FilteredDiagnosticSummary {
    pub key_lines: Vec<String>,
    pub tail_lines: Vec<String>,
    pub last_frame: Option<String>,
}

/// Filters raw log lines into key crash indicators and clean tail lines, bounded to limit capacities.
pub fn filter_diagnostics_log(raw_log: &str) -> FilteredDiagnosticSummary {
    let mut key = Vec::new();
    let mut tail = Vec::new();
    let mut last_frame = None;

    for line in raw_log.lines() {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }

        if trimmed.contains("presentation") {
            last_frame = Some(compact_threadtime_line(trimmed));
        }

        if is_key_line(trimmed) {
            key.push(compact_threadtime_line(trimmed));
        } else if trimmed.contains(" NFSU2 ") && !is_noise_line(trimmed) {
            tail.push(compact_threadtime_line(trimmed));
        }
    }

    if key.len() > 35 {
        let start = key.len() - 35;
        key = key[start..].to_vec();
    }
    if tail.len() > 60 {
        let start = tail.len() - 60;
        tail = tail[start..].to_vec();
    }

    FilteredDiagnosticSummary {
        key_lines: key,
        tail_lines: tail,
        last_frame,
    }
}

// ---------------------------------------------------------------------------
// JNI Export Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_Diagnostics_nativeFilterLogSummary(
    mut env: JNIEnv,
    _class: JClass,
    raw_log: JString,
) -> jstring {
    if raw_log.is_null() {
        return std::ptr::null_mut();
    }
    let log_str = match env.get_string(&raw_log) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return std::ptr::null_mut(),
    };

    let summary = filter_diagnostics_log(&log_str);

    // Encode as tab-separated sections:
    // SECTION_KEY\n<lines>\n---SECTION---\nSECTION_TAIL\n<lines>\n---SECTION---\nSECTION_FRAME\n<last_frame>
    let mut out = String::with_capacity(4096);
    out.push_str("KEY:\n");
    for k in summary.key_lines {
        out.push_str(&k);
        out.push('\n');
    }
    out.push_str("---DIV---\nTAIL:\n");
    for t in summary.tail_lines {
        out.push_str(&t);
        out.push('\n');
    }
    out.push_str("---DIV---\nFRAME:\n");
    if let Some(lf) = summary.last_frame {
        out.push_str(&lf);
    }

    env.new_string(out).map(|s| s.into_raw()).unwrap_or(std::ptr::null_mut())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compact_threadtime_line() {
        let line = "11-21 20:30:40.892 1234 5678 W NFSU2: Connecting to peer";
        let compacted = compact_threadtime_line(line);
        assert_eq!(compacted, "20:30:40.892 W Connecting to peer");
    }

    #[test]
    fn test_filter_diagnostics_log() {
        let log = "\
11-21 20:30:40.892 1234 5678 I NFSU2: Guest file loaded\n\
11-21 20:30:41.000 1234 5678 E NFSU2: FATAL SIGSEGV in libnfsu2.so\n\
11-21 20:30:42.000 1234 5678 I NFSU2: Starting race\n";
        let summary = filter_diagnostics_log(log);
        assert_eq!(summary.key_lines.len(), 1);
        assert!(summary.key_lines[0].contains("FATAL SIGSEGV"));
        assert_eq!(summary.tail_lines.len(), 1);
        assert!(summary.tail_lines[0].contains("Starting race"));
    }
}
