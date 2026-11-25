//! Pure Rust Lock-Free Diagnostic Telemetry & Performance Buffer for NFSU2 Android.
//!
//! Replaces Java `ProcessBuilder("logcat", ...)` regex parsing with a lock-free native
//! telemetry buffer recording real D3D9 Present intervals, rolling presentation FPS,
//! frame time, CPU usage (/proc/stat), and GPU utilization (/sys/class/kgsl/kgsl-3d0/gpubusy).

use std::fs;
use std::sync::atomic::{AtomicI8, AtomicU32, AtomicU64, AtomicUsize, Ordering};
use std::time::Instant;

const RING_SIZE: usize = 128;
const RING_MASK: usize = RING_SIZE - 1;

static FRAME_TIMESTAMPS: [AtomicU64; RING_SIZE] = {
    const ZERO: AtomicU64 = AtomicU64::new(0);
    [ZERO; RING_SIZE]
};

static RING_HEAD: AtomicUsize = AtomicUsize::new(0);
static LAST_FPS: AtomicU32 = AtomicU32::new(0); // stored as f32::to_bits
static LAST_FRAME_TIME_MS: AtomicU32 = AtomicU32::new(0); // stored as f32::to_bits
static TOTAL_FRAMES_RECORDED: AtomicU64 = AtomicU64::new(0);

// CPU tracking state
static PREV_TOTAL_JIFFIES: AtomicU64 = AtomicU64::new(0);
static PREV_WORK_JIFFIES: AtomicU64 = AtomicU64::new(0);
static LAST_CPU_PERCENT: AtomicU32 = AtomicU32::new(0);
static LAST_CPU_CHECK_MS: AtomicU64 = AtomicU64::new(0);

// GPU tracking state
static LAST_GPU_BUSY: AtomicU32 = AtomicU32::new(0);
static LAST_GPU_CHECK_MS: AtomicU64 = AtomicU64::new(0);
static GPU_SYSFS_STATE: AtomicI8 = AtomicI8::new(0); // 0=unchecked, 1=readable, -1=SELinux blocked
static mut GPU_SYSFS_WORKING_PATH: Option<String> = None;

/// Records a presentation frame timestamp in nanoseconds.
#[no_mangle]
pub extern "C" fn nfs_telemetry_record_present(timestamp_ns: u64) {
    let ts = if timestamp_ns == 0 {
        // Fallback to clock_gettime monotonic
        let mut tp = libc::timespec { tv_sec: 0, tv_nsec: 0 };
        unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut tp); }
        (tp.tv_sec as u64) * 1_000_000_000 + (tp.tv_nsec as u64)
    } else {
        timestamp_ns
    };

    let idx = RING_HEAD.fetch_add(1, Ordering::Relaxed) & RING_MASK;
    FRAME_TIMESTAMPS[idx].store(ts, Ordering::Relaxed);
    let total = TOTAL_FRAMES_RECORDED.fetch_add(1, Ordering::Relaxed) + 1;

    // Calculate rolling FPS over available frames (from 2 up to 60 frames)
    if total >= 2 {
        let window = (total as usize).min(60);
        let curr_idx = idx;
        let past_idx = idx.wrapping_sub(window - 1) & RING_MASK;
        let t_curr = FRAME_TIMESTAMPS[curr_idx].load(Ordering::Relaxed);
        let t_past = FRAME_TIMESTAMPS[past_idx].load(Ordering::Relaxed);

        if t_curr > t_past {
            let duration_ns = t_curr - t_past;
            let duration_sec = (duration_ns as f64) / 1_000_000_000.0;
            if duration_sec > 0.0001 {
                let fps = ((window - 1) as f64 / duration_sec) as f32;
                let frame_time_ms = (duration_sec * 1000.0 / (window - 1) as f64) as f32;
                LAST_FPS.store(fps.clamp(0.0, 360.0).to_bits(), Ordering::Relaxed);
                LAST_FRAME_TIME_MS.store(frame_time_ms.clamp(0.0, 1000.0).to_bits(), Ordering::Relaxed);
            }
        }

        // Also record single frame duration to FrameMetricsEngine in game_core if loaded
        let prev_idx = idx.wrapping_sub(1) & RING_MASK;
        let t_prev = FRAME_TIMESTAMPS[prev_idx].load(Ordering::Relaxed);
        if t_curr > t_prev && t_prev > 0 {
            let single_dur_ns = t_curr - t_prev;
            static mut RECORD_FN: Option<extern "C" fn(u64)> = None;
            static INIT: std::sync::Once = std::sync::Once::new();
            INIT.call_once(|| unsafe {
                let sym = libc::dlsym(libc::RTLD_DEFAULT, b"nfs_metrics_record_frame\0".as_ptr() as *const _);
                if !sym.is_null() {
                    RECORD_FN = Some(std::mem::transmute(sym));
                }
            });
            unsafe {
                if let Some(f) = RECORD_FN {
                    f(single_dur_ns);
                }
            }
        }
    }
}

/// Returns the current dynamic presentation FPS.
#[no_mangle]
pub extern "C" fn nfs_telemetry_get_fps() -> f32 {
    f32::from_bits(LAST_FPS.load(Ordering::Relaxed))
}

/// Returns the current average frame duration in milliseconds.
#[no_mangle]
pub extern "C" fn nfs_telemetry_get_frame_time_ms() -> f32 {
    f32::from_bits(LAST_FRAME_TIME_MS.load(Ordering::Relaxed))
}

/// Returns the current total CPU utilization percentage (0.0 to 100.0).
#[no_mangle]
pub extern "C" fn nfs_telemetry_get_cpu_usage() -> f32 {
    let mut tp = libc::timespec { tv_sec: 0, tv_nsec: 0 };
    unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut tp); }
    let now_ms = (tp.tv_sec as u64) * 1000 + (tp.tv_nsec as u64) / 1_000_000;
    let last_check = LAST_CPU_CHECK_MS.load(Ordering::Relaxed);

    // Refresh at most every 250ms
    if now_ms < last_check + 250 && last_check > 0 {
        return f32::from_bits(LAST_CPU_PERCENT.load(Ordering::Relaxed));
    }
    LAST_CPU_CHECK_MS.store(now_ms, Ordering::Relaxed);

    if let Ok(stat) = fs::read_to_string("/proc/stat") {
        if let Some(first_line) = stat.lines().next() {
            if first_line.starts_with("cpu ") {
                let parts: Vec<u64> = first_line[4..]
                    .split_whitespace()
                    .filter_map(|s| s.parse::<u64>().ok())
                    .collect();

                if parts.len() >= 4 {
                    let user = parts[0];
                    let nice = parts[1];
                    let system = parts[2];
                    let idle = parts[3];
                    let iowait = parts.get(4).copied().unwrap_or(0);
                    let irq = parts.get(5).copied().unwrap_or(0);
                    let softirq = parts.get(6).copied().unwrap_or(0);

                    let work = user + nice + system + irq + softirq;
                    let total = work + idle + iowait;

                    let prev_total = PREV_TOTAL_JIFFIES.swap(total, Ordering::Relaxed);
                    let prev_work = PREV_WORK_JIFFIES.swap(work, Ordering::Relaxed);

                    if prev_total > 0 && total > prev_total {
                        let total_diff = total - prev_total;
                        let work_diff = work.saturating_sub(prev_work);
                        let percent = (work_diff as f32 / total_diff as f32) * 100.0;
                        let clamped = percent.clamp(0.0, 100.0);
                        LAST_CPU_PERCENT.store(clamped.to_bits(), Ordering::Relaxed);
                        return clamped;
                    }
                }
            }
        }
    }

    // Fallback: unprivileged /proc/self/stat (Process CPU usage across all active cores)
    if let Ok(self_stat) = fs::read_to_string("/proc/self/stat") {
        if let Some(rparen) = self_stat.rfind(')') {
            let rest = &self_stat[rparen + 1..];
            let fields: Vec<&str> = rest.split_whitespace().collect();
            // fields[11] is utime (14th field in /proc/pid/stat), fields[12] is stime (15th field)
            if fields.len() >= 13 {
                if let (Ok(utime), Ok(stime)) = (fields[11].parse::<u64>(), fields[12].parse::<u64>()) {
                    let work = utime + stime;
                    let prev_work = PREV_WORK_JIFFIES.swap(work, Ordering::Relaxed);
                    let prev_time = PREV_TOTAL_JIFFIES.swap(now_ms, Ordering::Relaxed);
                    if prev_time > 0 && now_ms > prev_time {
                        let time_diff_s = (now_ms - prev_time) as f32 / 1000.0;
                        let work_diff = work.saturating_sub(prev_work);
                        let total_cores = std::thread::available_parallelism()
                            .map(|n| n.get())
                            .unwrap_or(8) as f32;
                        // sysconf(_SC_CLK_TCK) is standard 100 on Linux / Android
                        // Normalize by total available cores so CPU usage is strictly 0.0% to 100.0%
                        let cpu_percent = ((work_diff as f32 / 100.0) / time_diff_s * 100.0) / total_cores.max(1.0);
                        let clamped = cpu_percent.clamp(0.0, 100.0);
                        LAST_CPU_PERCENT.store(clamped.to_bits(), Ordering::Relaxed);
                        return clamped;
                    }
                }
            }
        }
    }

    f32::from_bits(LAST_CPU_PERCENT.load(Ordering::Relaxed))
}

/// Returns the current Qualcomm Adreno / Mali GPU busy percentage (0.0 to 100.0).
#[no_mangle]
pub extern "C" fn nfs_telemetry_get_gpu_busy() -> f32 {
    if GPU_SYSFS_STATE.load(Ordering::Relaxed) < 0 {
        return f32::from_bits(LAST_GPU_BUSY.load(Ordering::Relaxed));
    }

    let mut tp = libc::timespec { tv_sec: 0, tv_nsec: 0 };
    unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut tp); }
    let now_ms = (tp.tv_sec as u64) * 1000 + (tp.tv_nsec as u64) / 1_000_000;
    let last_check = LAST_GPU_CHECK_MS.load(Ordering::Relaxed);

    // Refresh at most every 250ms
    if now_ms < last_check + 250 && last_check > 0 {
        return f32::from_bits(LAST_GPU_BUSY.load(Ordering::Relaxed));
    }
    LAST_GPU_CHECK_MS.store(now_ms, Ordering::Relaxed);

    // Fast path: reuse previously discovered working sysfs node without re-probing denied paths
    unsafe {
        if let Some(ref p) = GPU_SYSFS_WORKING_PATH {
            if let Ok(content) = fs::read_to_string(p) {
                let trimmed = content.trim().trim_end_matches('%').trim();
                let mut parts = trimmed.split_whitespace();
                if let Some(first) = parts.next() {
                    if let Some(second) = parts.next() {
                        if let (Ok(busy), Ok(total)) = (first.parse::<u64>(), second.parse::<u64>()) {
                            if total > 0 && busy <= total {
                                let clamped = ((busy as f32 / total as f32) * 100.0).clamp(0.0, 100.0);
                                LAST_GPU_BUSY.store(clamped.to_bits(), Ordering::Relaxed);
                                return clamped;
                            }
                        }
                    } else if let Ok(val) = first.parse::<f32>() {
                        if (0.0..=100.0).contains(&val) {
                            LAST_GPU_BUSY.store(val.to_bits(), Ordering::Relaxed);
                            return val;
                        }
                    }
                }
            }
            GPU_SYSFS_WORKING_PATH = None;
            GPU_SYSFS_STATE.store(-1, Ordering::Relaxed);
            return f32::from_bits(LAST_GPU_BUSY.load(Ordering::Relaxed));
        }
    }

    // 1. Qualcomm Adreno kgsl sysfs nodes
    let adreno_paths = [
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/class/kgsl/kgsl-3d0/gpu_load",
        "/sys/class/kgsl/kgsl-3d0/gpubusy",
        "/sys/devices/platform/soc/soc:qcom,kgsl-3d0/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/devices/platform/soc/soc:qcom,kgsl-3d0/kgsl/kgsl-3d0/gpubusy",
        "/sys/devices/platform/soc/soc:qcom,kgsl-3d0/kgsl/kgsl-3d0/gpu_load",
    ];

    for path in &adreno_paths {
        if let Ok(content) = fs::read_to_string(path) {
            let trimmed = content.trim().trim_end_matches('%').trim();
            let mut parts = trimmed.split_whitespace();
            if let Some(first) = parts.next() {
                if let Some(second) = parts.next() {
                    if let (Ok(busy), Ok(total)) = (first.parse::<u64>(), second.parse::<u64>()) {
                        if total > 0 && busy <= total {
                            let pct = (busy as f32 / total as f32) * 100.0;
                            let clamped = pct.clamp(0.0, 100.0);
                            unsafe { GPU_SYSFS_WORKING_PATH = Some((*path).to_string()); }
                            GPU_SYSFS_STATE.store(1, Ordering::Relaxed);
                            LAST_GPU_BUSY.store(clamped.to_bits(), Ordering::Relaxed);
                            return clamped;
                        }
                    }
                } else if let Ok(val) = first.parse::<f32>() {
                    if (0.0..=100.0).contains(&val) {
                        unsafe { GPU_SYSFS_WORKING_PATH = Some((*path).to_string()); }
                        GPU_SYSFS_STATE.store(1, Ordering::Relaxed);
                        LAST_GPU_BUSY.store(val.to_bits(), Ordering::Relaxed);
                        return val;
                    }
                }
            }
        }
    }

    // Dynamic search in /sys/devices/platform/soc for kgsl
    if let Ok(entries) = fs::read_dir("/sys/devices/platform/soc") {
        for entry in entries.flatten() {
            let name = entry.file_name();
            let name_str = name.to_string_lossy();
            if name_str.contains("kgsl") {
                let p_busy = entry.path().join("kgsl/kgsl-3d0/gpubusy");
                if let Ok(content) = fs::read_to_string(&p_busy) {
                    let mut parts = content.split_whitespace();
                    if let (Some(b_str), Some(t_str)) = (parts.next(), parts.next()) {
                        if let (Ok(busy), Ok(total)) = (b_str.parse::<u64>(), t_str.parse::<u64>()) {
                            if total > 0 && busy <= total {
                                let pct = (busy as f32 / total as f32) * 100.0;
                                let clamped = pct.clamp(0.0, 100.0);
                                unsafe { GPU_SYSFS_WORKING_PATH = Some(p_busy.to_string_lossy().into_owned()); }
                                GPU_SYSFS_STATE.store(1, Ordering::Relaxed);
                                LAST_GPU_BUSY.store(clamped.to_bits(), Ordering::Relaxed);
                                return clamped;
                            }
                        }
                    }
                }
                let p_pct = entry.path().join("kgsl/kgsl-3d0/gpu_busy_percentage");
                if let Ok(content) = fs::read_to_string(&p_pct) {
                    let trimmed = content.trim().trim_end_matches('%').trim();
                    if let Ok(val) = trimmed.parse::<f32>() {
                        if (0.0..=100.0).contains(&val) {
                            unsafe { GPU_SYSFS_WORKING_PATH = Some(p_pct.to_string_lossy().into_owned()); }
                            GPU_SYSFS_STATE.store(1, Ordering::Relaxed);
                            LAST_GPU_BUSY.store(val.to_bits(), Ordering::Relaxed);
                            return val;
                        }
                    }
                }
            }
        }
    }

    // 2. ARM Mali & MediaTek GED sysfs utilization nodes
    let mali_paths = [
        "/sys/class/misc/mali0/device/gpu_busy",
        "/sys/class/misc/mali0/device/utilization",
        "/sys/devices/platform/mali.0/utilization",
        "/sys/devices/platform/13040000.mali/utilization",
        "/sys/devices/platform/13000000.mali/utilization",
        "/sys/devices/platform/14ac0000.mali/utilization",
        "/sys/devices/platform/1c500000.mali/utilization",
        "/sys/devices/platform/sprd_mali.0/utilization",
        "/sys/devices/platform/gpu/utilization",
        "/sys/kernel/debug/ged/hal/gpu_utilization",
        "/sys/module/ged/parameters/gpu_loading",
        "/sys/module/ged/parameters/gpu_dvfs_loading",
    ];
    for path in &mali_paths {
        if let Ok(content) = fs::read_to_string(path) {
            let trimmed = content.trim().trim_end_matches('%').trim();
            if let Ok(val) = trimmed.split_whitespace().next().unwrap_or("").parse::<f32>() {
                if (0.0..=100.0).contains(&val) {
                    let clamped = val.clamp(0.0, 100.0);
                    unsafe { GPU_SYSFS_WORKING_PATH = Some((*path).to_string()); }
                    GPU_SYSFS_STATE.store(1, Ordering::Relaxed);
                    LAST_GPU_BUSY.store(clamped.to_bits(), Ordering::Relaxed);
                    return clamped;
                }
            }
        }
    }

    GPU_SYSFS_STATE.store(-1, Ordering::Relaxed);
    f32::from_bits(LAST_GPU_BUSY.load(Ordering::Relaxed))
}

// ---- JNI Interface for io.mininxd.nfsu2.DiagnosticOverlay -----------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_DiagnosticOverlay_nativeGetTelemetry(
    mut env: jni::JNIEnv,
    _class: jni::objects::JClass,
    out_array: jni::sys::jfloatArray,
) -> jni::sys::jboolean {
    if out_array.is_null() {
        return 0;
    }

    let fps = nfs_telemetry_get_fps();
    let frame_time_ms = nfs_telemetry_get_frame_time_ms();
    let cpu_pct = nfs_telemetry_get_cpu_usage();
    let gpu_busy = nfs_telemetry_get_gpu_busy();

    let values = [fps, frame_time_ms, cpu_pct, gpu_busy];
    let j_arr = jni::objects::JFloatArray::from_raw(out_array);
    if env.set_float_array_region(&j_arr, 0, &values).is_ok() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_DiagnosticOverlay_nativeRecordPresent(
    _env: jni::JNIEnv,
    _class: jni::objects::JClass,
    frame_duration_ns: jni::sys::jlong,
) {
    nfs_telemetry_record_present(frame_duration_ns as u64);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_telemetry_fps_calculation() {
        let base_ns = 1_000_000_000;
        let interval_ns = 16_666_666; // 60 FPS (~16.67ms)

        for i in 0..70 {
            nfs_telemetry_record_present(base_ns + i * interval_ns);
        }

        let fps = nfs_telemetry_get_fps();
        let frame_time = nfs_telemetry_get_frame_time_ms();

        assert!((fps - 60.0).abs() < 1.0);
        assert!((frame_time - 16.67).abs() < 0.5);
    }
}
