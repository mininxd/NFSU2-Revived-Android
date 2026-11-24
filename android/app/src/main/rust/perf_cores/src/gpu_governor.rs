//! GPU DVFS Governor & Peak Frequency Sustainer for NFSU2 Android.
//!
//! Addresses the critical GPU clock and usage drop during races and open-world exploring:
//! - In the garage / showroom, lightweight CPU logic emits back-to-back draw calls,
//!   saturating the GPU command ring buffer and locking the mobile GPU at peak frequencies.
//! - In races and open-world exploring, heavy x86 CPU emulation (physics, AI, world streaming,
//!   Miles audio) creates a 20-30ms gap between frame submissions.
//! - The GPU finishes rendering in only 3-5ms. Because the GPU sits idle during the remaining
//!   frame interval, kernel GPU governors (Qualcomm Adreno kgsl idle_timer, ARM Mali GED / devfreq)
//!   mistakenly downclock the GPU to low-power idle states (200-300 MHz, PwrLevel 5/6).
//! - When the next frame's draw calls arrive, the GPU is at rock-bottom clock, causing massive
//!   frame spikes, stuttering, and dropping reported GPU usage.
//!
//! This module provides:
//! 1. Adreno kgsl-3d0 sysfs tuning: sets min_pwrlevel=0, extends idle_timer to 1000ms,
//!    locks force_bus_on/force_clk_on/force_rail_on, and clamps devfreq min_freq to maximum.
//! 2. ARM Mali & MediaTek Dimensity GED tuning: sets boost_gpu_enable=1, ged_boost_enable=1,
//!    gpu_boost_level=100, and clamps devfreq to peak frequency.
//! 3. Background Watchdog Keeper: re-asserts clock locks every 500ms so OEM power daemons
//!    (Xiaomi Joyose, Qualcomm thermal-engine, Samsung GOS) cannot revert GPU clocks.
//! 4. In-gameplay state awareness: dynamically escalates governor aggression during races and roam.

use std::fs;
use std::os::raw::c_int;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::thread;
use std::time::Duration;
use jni::JNIEnv;
use jni::objects::JClass;
use jni::sys::jboolean;

static GOVERNOR_RUNNING: AtomicBool = AtomicBool::new(false);
static GAMEPLAY_ACTIVE: AtomicBool = AtomicBool::new(false);
static LAST_LOCKED_FREQ: AtomicI32 = AtomicI32::new(0);

/// Supported GPU Vendor Architectures.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GpuVendor {
    Adreno,
    Mali,
    Generic,
}

/// Detects the underlying mobile GPU vendor from sysfs nodes.
pub fn detect_gpu_vendor() -> GpuVendor {
    if fs::metadata("/sys/class/kgsl/kgsl-3d0").is_ok()
        || fs::metadata("/sys/devices/platform/soc/soc:qcom,kgsl-3d0").is_ok()
    {
        return GpuVendor::Adreno;
    }

    if fs::metadata("/sys/class/misc/mali0").is_ok()
        || fs::metadata("/sys/module/ged").is_ok()
        || fs::metadata("/sys/kernel/debug/ged").is_ok()
    {
        return GpuVendor::Mali;
    }

    GpuVendor::Generic
}

/// Helper to write a string value to a sysfs path, ignoring missing files/permission errors.
fn write_sysfs(path: &str, value: &str) -> bool {
    if let Ok(()) = fs::write(path, value) {
        true
    } else {
        false
    }
}

/// Helper to read a sysfs file trimmed.
fn read_sysfs(path: &str) -> Option<String> {
    fs::read_to_string(path).ok().map(|s| s.trim().to_string())
}

/// Parses available frequencies and returns the highest frequency in kHz / Hz.
fn parse_max_freq(path: &str) -> Option<u64> {
    let content = fs::read_to_string(path).ok()?;
    let mut max_freq = 0u64;
    for part in content.split_whitespace() {
        if let Ok(freq) = part.parse::<u64>() {
            if freq > max_freq {
                max_freq = freq;
            }
        }
    }
    if max_freq > 0 { Some(max_freq) } else { None }
}

/// Locks Qualcomm Adreno GPU clocks and extends idle_timer to prevent downclocking.
pub fn lock_adreno_peak_clocks(is_gameplay: bool) {
    let kgsl_paths = [
        "/sys/class/kgsl/kgsl-3d0",
        "/sys/devices/platform/soc/soc:qcom,kgsl-3d0/kgsl/kgsl-3d0",
        "/sys/devices/platform/soc/1c00000.qcom,kgsl-3d0/kgsl/kgsl-3d0",
        "/sys/devices/platform/soc/3d00000.qcom,kgsl-3d0/kgsl/kgsl-3d0",
    ];

    for &base in &kgsl_paths {
        if fs::metadata(base).is_err() {
            continue;
        }

        // 1. Min/Max power levels: 0 is maximum frequency on Qualcomm Adreno
        write_sysfs(&format!("{}/min_pwrlevel", base), "0");
        write_sysfs(&format!("{}/default_pwrlevel", base), "0");
        write_sysfs(&format!("{}/thermal_pwrlevel", base), "0");

        // 2. CRITICAL FIX: idle_timer
        // By default, Adreno KGSL sets idle_timer to 12-80ms.
        // During races/exploring, CPU emulation takes 20-30ms, causing the GPU
        // to immediately drop to PwrLevel 5/6 (lowest idle clock).
        // Extending idle_timer to 1000ms prevents the GPU from sleeping between CPU frames!
        let idle_timeout = if is_gameplay { "1000" } else { "500" };
        write_sysfs(&format!("{}/idle_timer", base), idle_timeout);

        // 3. Force rails, clocks, and bus votes on so GPU hardware stays awake
        write_sysfs(&format!("{}/force_rail_on", base), "1");
        write_sysfs(&format!("{}/force_clk_on", base), "1");
        write_sysfs(&format!("{}/force_bus_on", base), "1");

        // 4. Devfreq governor and frequency clamping
        let devfreq_base = format!("{}/devfreq", base);
        if fs::metadata(&devfreq_base).is_ok() {
            write_sysfs(&format!("{}/governor", devfreq_base), "performance");

            // Look for available frequencies and write peak clock to min_freq
            let avail_paths = [
                format!("{}/available_frequencies", devfreq_base),
                format!("{}/gpu_available_frequencies", base),
            ];
            for avail in &avail_paths {
                if let Some(peak_freq) = parse_max_freq(avail) {
                    write_sysfs(&format!("{}/min_freq", devfreq_base), &peak_freq.to_string());
                    LAST_LOCKED_FREQ.store((peak_freq / 1000) as i32, Ordering::Relaxed);
                    break;
                }
            }
        }
    }
}

/// Locks ARM Mali & MediaTek Dimensity GPU clocks and activates GED performance boost.
pub fn lock_mali_peak_clocks(is_gameplay: bool) {
    // 1. MediaTek GED (Graphics Energy Dynamic) kernel parameters
    write_sysfs("/sys/module/ged/parameters/boost_gpu_enable", "1");
    write_sysfs("/sys/module/ged/parameters/ged_boost_enable", "1");
    write_sysfs("/sys/module/ged/parameters/gx_boost_on", "1");
    write_sysfs("/sys/module/ged/parameters/gx_force_cpu_boost", "1");
    write_sysfs("/sys/kernel/debug/ged/hal/gpu_boost_level", "100");

    if is_gameplay {
        write_sysfs("/sys/module/ged/parameters/gpu_dvfs_enable", "1");
        write_sysfs("/sys/kernel/debug/ged/hal/force_max_fps", "1");
    }

    // 2. Mali devfreq sysfs nodes
    let mali_devfreq_candidates = [
        "/sys/class/misc/mali0/device/devfreq",
        "/sys/devices/platform/13040000.mali/devfreq",
        "/sys/devices/platform/13000000.mali/devfreq",
        "/sys/devices/platform/gpu/devfreq",
    ];

    for &mali_base in &mali_devfreq_candidates {
        if let Ok(entries) = fs::read_dir(mali_base) {
            for entry in entries.flatten() {
                let sub_path = entry.path();
                if sub_path.is_dir() {
                    let gov_file = sub_path.join("governor");
                    let _ = fs::write(&gov_file, "performance");

                    let avail_file = sub_path.join("available_frequencies");
                    if let Some(peak_freq) = parse_max_freq(&avail_file.to_string_lossy()) {
                        let min_file = sub_path.join("min_freq");
                        let _ = fs::write(&min_file, peak_freq.to_string());
                        LAST_LOCKED_FREQ.store((peak_freq / 1000) as i32, Ordering::Relaxed);
                    }
                }
            }
        }
    }
}

/// Applies universal GPU clock locking according to detected hardware.
pub fn apply_gpu_clock_lock(is_gameplay: bool) {
    match detect_gpu_vendor() {
        GpuVendor::Adreno => lock_adreno_peak_clocks(is_gameplay),
        GpuVendor::Mali => lock_mali_peak_clocks(is_gameplay),
        GpuVendor::Generic => {
            // Apply both sets of nodes in case vendor identifiers were masked
            lock_adreno_peak_clocks(is_gameplay);
            lock_mali_peak_clocks(is_gameplay);
        }
    }
}

/// Starts the persistent GPU Governor Keeper thread.
/// Runs in the background and re-applies GPU frequency clamps every 500ms,
/// preventing OEM background daemons (thermal-engine, Joyose, GOS) from downclocking.
pub fn start_gpu_governor_keeper() {
    if GOVERNOR_RUNNING.swap(true, Ordering::SeqCst) {
        return; // Already running
    }

    // Immediate initial lock
    apply_gpu_clock_lock(GAMEPLAY_ACTIVE.load(Ordering::Relaxed));

    thread::Builder::new()
        .name("nfs_gpu_keeper".to_string())
        .spawn(move || {
            while GOVERNOR_RUNNING.load(Ordering::Relaxed) {
                let is_gameplay = GAMEPLAY_ACTIVE.load(Ordering::Relaxed);
                apply_gpu_clock_lock(is_gameplay);

                // Sleep 500ms between keeper pulses
                thread::sleep(Duration::from_millis(500));
            }
        })
        .ok();
}

/// Stops the background GPU Governor Keeper thread.
pub fn stop_gpu_governor_keeper() {
    GOVERNOR_RUNNING.store(false, Ordering::SeqCst);
}

/// Notifies the GPU governor of active racing or open-world exploring.
pub fn set_gameplay_state(in_gameplay: bool) {
    GAMEPLAY_ACTIVE.store(in_gameplay, Ordering::Relaxed);
    // Immediately pulse clock lock with gameplay aggression
    apply_gpu_clock_lock(in_gameplay);
}

/// Frame tick hook: called on each frame present to keep the governor engaged.
pub fn on_frame_tick(in_gameplay: bool) {
    if in_gameplay != GAMEPLAY_ACTIVE.load(Ordering::Relaxed) {
        set_gameplay_state(in_gameplay);
    }
}

// ---------------------------------------------------------------------------
// C ABI Exports (callable via dlsym or direct C linkage)
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_governor_init() {
    start_gpu_governor_keeper();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_governor_lock_peak() {
    apply_gpu_clock_lock(GAMEPLAY_ACTIVE.load(Ordering::Relaxed));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_governor_set_gameplay_state(in_gameplay: c_int) {
    set_gameplay_state(in_gameplay != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_governor_tick(in_gameplay: c_int) {
    on_frame_tick(in_gameplay != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_governor_destroy() {
    stop_gpu_governor_keeper();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gpu_governor_get_locked_freq_mhz() -> c_int {
    LAST_LOCKED_FREQ.load(Ordering::Relaxed)
}

// ---------------------------------------------------------------------------
// JNI Exports for io.mininxd.nfsu2.GpuClockGovernor
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GpuClockGovernor_nativeInitGovernor(
    _env: JNIEnv,
    _class: JClass,
) {
    start_gpu_governor_keeper();
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GpuClockGovernor_nativeLockPeakClocks(
    _env: JNIEnv,
    _class: JClass,
    is_gameplay: jboolean,
) {
    apply_gpu_clock_lock(is_gameplay != 0);
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GpuClockGovernor_nativeSetGameplayActive(
    _env: JNIEnv,
    _class: JClass,
    is_gameplay: jboolean,
) {
    set_gameplay_state(is_gameplay != 0);
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GpuClockGovernor_nativeStopGovernor(
    _env: JNIEnv,
    _class: JClass,
) {
    stop_gpu_governor_keeper();
}
