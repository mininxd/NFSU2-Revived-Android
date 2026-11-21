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

/// Formats the telemetry overlay HUD text in pure Rust without JVM String garbage collection.
pub fn format_overlay_text(
    is_full: bool,
    fps: f32,
    p1_low: f32,
    frame_time_ms: f32,
    cpu_percent: f32,
    gpu_percent: f32,
    used_mb: u64,
    total_mb: u64,
    roam_enabled: bool,
    adhoc_connected: bool,
    is_racing: bool,
    on_race_count: i32,
    racers_csv: &str,
    display_online: i32,
    server_host: &str,
    room_name: &str,
    room_roam: i32,
    total_roam: i32,
    total_server: i32,
    conn_status: &str,
    gpu_driver: &str,
    cpu_model: &str,
    system_platform: &str,
) -> String {
    let fps_str = if fps >= 100.0 {
        format!("{:.0}", fps)
    } else {
        format!("{:.1}", fps)
    };

    if is_full {
        let mut s = String::with_capacity(512);
        s.push_str("fps: ");
        s.push_str(&fps_str);
        if p1_low > 0.0 {
            s.push_str(&format!(" (1% low: {:.0})", p1_low));
        }
        s.push_str(&format!(" ({:.1} ms)\n", frame_time_ms));

        if roam_enabled {
            if adhoc_connected {
                s.push_str(&format!("network: connected to {}\n", server_host));
                if !room_name.trim().is_empty() && room_roam >= 0 {
                    s.push_str(&format!(
                        "player_online: {} (room: \"{}\", roam: {}, server: {})\n",
                        room_roam, room_name.trim(), total_roam, total_server
                    ));
                } else if total_roam > 0 {
                    s.push_str(&format!("player_online: {} (server: {})\n", total_roam, total_server));
                } else if total_server > 0 {
                    s.push_str(&format!("player_online: {}\n", total_server));
                } else {
                    s.push_str(&format!("player_online: {}\n", display_online));
                }

                if is_racing {
                    s.push_str(&format!("player_on_race: {}\n", on_race_count));
                    s.push_str("player_racing: [");
                    s.push_str(racers_csv);
                    s.push_str("]\n");
                }
            } else {
                s.push_str(&format!("network: {}\n", conn_status));
            }
        }

        s.push_str(&format!("cpu_usage: {:.1}%\n", cpu_percent));
        s.push_str(&format!("gpu_usage: {:.0}%\n", gpu_percent));
        s.push_str(&format!("{}, {}, {}\n", gpu_driver, cpu_model, system_platform));
        s.push_str(&format!("memory: {} MB / {} MB", used_mb, total_mb));
        s
    } else {
        let ram_used_gb = used_mb as f32 / 1024.0;
        let ram_total_gb = total_mb as f32 / 1024.0;
        if adhoc_connected {
            if is_racing {
                format!(
                    "FPS: {} ({:.1} ms)  |  Racing: {} [{}] (Online: {})\nCPU: {:.0}%  |  GPU: {:.0}%  |  RAM: {:.1}/{:.1} GB",
                    fps_str, frame_time_ms, on_race_count, racers_csv, display_online, cpu_percent, gpu_percent, ram_used_gb, ram_total_gb
                )
            } else {
                format!(
                    "FPS: {} ({:.1} ms)  |  Online: {}  |  connected to {}\nCPU: {:.0}%  |  GPU: {:.0}%  |  RAM: {:.1}/{:.1} GB",
                    fps_str, frame_time_ms, display_online, server_host, cpu_percent, gpu_percent, ram_used_gb, ram_total_gb
                )
            }
        } else {
            format!(
                "FPS: {} ({:.1} ms)  |  Offline\nCPU: {:.0}%  |  GPU: {:.0}%  |  RAM: {:.1}/{:.1} GB",
                fps_str, frame_time_ms, cpu_percent, gpu_percent, ram_used_gb, ram_total_gb
            )
        }
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_DiagnosticOverlay_nativeFormatOverlayText(
    mut env: JNIEnv,
    _class: JClass,
    is_full: jni::sys::jboolean,
    fps: jni::sys::jfloat,
    p1_low: jni::sys::jfloat,
    frame_time_ms: jni::sys::jfloat,
    cpu_percent: jni::sys::jfloat,
    gpu_percent: jni::sys::jfloat,
    used_mb: jni::sys::jlong,
    total_mb: jni::sys::jlong,
    roam_enabled: jni::sys::jboolean,
    adhoc_connected: jni::sys::jboolean,
    is_racing: jni::sys::jboolean,
    on_race_count: jni::sys::jint,
    racers_csv: JString,
    display_online: jni::sys::jint,
    server_host: JString,
    room_name: JString,
    room_roam: jni::sys::jint,
    total_roam: jni::sys::jint,
    total_server: jni::sys::jint,
    conn_status: JString,
    gpu_driver: JString,
    cpu_model: JString,
    system_platform: JString,
) -> jstring {
    let get_str = |env: &mut JNIEnv, js: &JString| -> String {
        if js.is_null() {
            String::new()
        } else {
            env.get_string(js).map(|s| s.to_string_lossy().to_string()).unwrap_or_default()
        }
    };

    let r_csv = get_str(&mut env, &racers_csv);
    let s_host = get_str(&mut env, &server_host);
    let r_name = get_str(&mut env, &room_name);
    let c_status = get_str(&mut env, &conn_status);
    let g_driver = get_str(&mut env, &gpu_driver);
    let c_model = get_str(&mut env, &cpu_model);
    let s_plat = get_str(&mut env, &system_platform);

    let res = format_overlay_text(
        is_full != 0,
        fps,
        p1_low,
        frame_time_ms,
        cpu_percent,
        gpu_percent,
        used_mb as u64,
        total_mb as u64,
        roam_enabled != 0,
        adhoc_connected != 0,
        is_racing != 0,
        on_race_count,
        &r_csv,
        display_online,
        &s_host,
        &r_name,
        room_roam,
        total_roam,
        total_server,
        &c_status,
        &g_driver,
        &c_model,
        &s_plat,
    );

    env.new_string(res).map(|s| s.into_raw()).unwrap_or(std::ptr::null_mut())
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
