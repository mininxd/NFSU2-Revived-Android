//! CPU Performance Cores affinity detection and pinning for NFSU2 Android Launcher.
//!
//! Modern Android SoC chips (Snapdragon, Dimensity, Tensor, Exynos) employ big.LITTLE
//! or DynamIQ heterogeneous architectures containing:
//!  - LITTLE Cores (Efficiency / battery saver, e.g. Cortex-A55 / A510, ~1.8-2.0 GHz)
//!  - MID / BIG Cores (Performance, e.g. Cortex-A78 / A715, ~2.8-3.0 GHz)
//!  - PRIME Cores (Ultra-performance, e.g. Cortex-X1 / X3 / X4, ~3.2-3.4 GHz)
//!
//! If the process defaults to CPU 0 (a LITTLE efficiency core) or SingleCoreAffinity is on,
//! the game suffers from severe frame drops and stuttering. This module identifies the
//! high-frequency Performance cores and applies process/thread affinity to ensure
//! the guest emulation and Vulkan driver always run on the fastest cores.

use std::fs;
use std::os::raw::{c_int, c_long};
use jni::JNIEnv;
use jni::objects::{JClass, JString};
use jni::sys::{jint, jlong, jstring};

/// Represents information about a single CPU core.
#[derive(Debug, Clone, Copy)]
pub struct CoreInfo {
    pub cpu_id: usize,
    pub max_freq_khz: u64,
    pub is_performance: bool,
}

/// Topology detection result.
#[derive(Debug, Clone)]
pub struct CpuCluster {
    pub total_cores: usize,
    pub perf_cores_mask: u64,
    pub perf_core_count: usize,
    pub cores: Vec<CoreInfo>,
}

impl CpuCluster {
    /// Detects the device CPU topology by inspecting sysfs cpufreq nodes.
    pub fn detect() -> Self {
        let mut cores = Vec::new();
        let total_cores = Self::detect_total_cores();
        let mut highest_freq: u64 = 0;

        for id in 0..total_cores {
            let freq_path = format!("/sys/devices/system/cpu/cpu{}/cpufreq/cpuinfo_max_freq", id);
            let freq = fs::read_to_string(&freq_path)
                .ok()
                .and_then(|s| s.trim().parse::<u64>().ok())
                .unwrap_or(0);

            if freq > highest_freq {
                highest_freq = freq;
            }

            cores.push(CoreInfo {
                cpu_id: id,
                max_freq_khz: freq,
                is_performance: false,
            });
        }

        // Performance threshold: cores within 80% of the SoC's highest frequency
        let threshold = if highest_freq > 0 {
            (highest_freq as f64 * 0.80) as u64
        } else {
            0
        };

        let mut perf_cores_mask: u64 = 0;
        let mut perf_core_count = 0;

        for core in &mut cores {
            if highest_freq > 0 && core.max_freq_khz >= threshold {
                core.is_performance = true;
                perf_cores_mask |= 1u64 << core.cpu_id;
                perf_core_count += 1;
            }
        }

        // Fallback: If frequency detection wasn't readable, treat upper half of cores as performance
        if perf_cores_mask == 0 && total_cores > 1 {
            let start = total_cores / 2;
            for id in start..total_cores {
                perf_cores_mask |= 1u64 << id;
                perf_core_count += 1;
                if let Some(c) = cores.get_mut(id) {
                    c.is_performance = true;
                }
            }
        } else if perf_cores_mask == 0 {
            perf_cores_mask = 1;
            perf_core_count = 1;
        }

        CpuCluster {
            total_cores,
            perf_cores_mask,
            perf_core_count,
            cores,
        }
    }

    fn detect_total_cores() -> usize {
        if let Ok(content) = fs::read_to_string("/sys/devices/system/cpu/possible") {
            let trimmed = content.trim();
            if let Some(pos) = trimmed.rfind('-') {
                if let Ok(end) = trimmed[pos + 1..].parse::<usize>() {
                    return end + 1;
                }
            }
        }
        num_cpus_fallback()
    }

    /// Binds the calling thread or process to the performance cores.
    pub fn apply_affinity(&self) -> Result<(), i32> {
        let mask = self.perf_cores_mask;
        unsafe {
            // Linux sched_setaffinity: 0 = calling thread, getpid() = entire process
            let mask_val = mask as c_long;
            let _ = libc::syscall(
                libc::SYS_sched_setaffinity,
                libc::getpid() as c_int,
                std::mem::size_of::<c_long>(),
                &mask_val as *const c_long,
            );
            let ret = libc::syscall(
                libc::SYS_sched_setaffinity,
                0 as c_int,
                std::mem::size_of::<c_long>(),
                &mask_val as *const c_long,
            );
            if ret == 0 {
                Ok(())
            } else {
                Err(std::io::Error::last_os_error().raw_os_error().unwrap_or(-1))
            }
        }
    }
}

fn num_cpus_fallback() -> usize {
    let count = unsafe { libc::sysconf(libc::_SC_NPROCESSORS_CONF) };
    if count > 0 { count as usize } else { 8 }
}

// ---- C ABI Exports ---------------------------------------------------------------------------------

/// Binds the calling process to performance cores. Returns 0 on success, or -1 on error.
#[no_mangle]
pub extern "C" fn nfs_perf_cores_bind() -> c_int {
    let cluster = CpuCluster::detect();
    match cluster.apply_affinity() {
        Ok(_) => 0,
        Err(_) => -1,
    }
}

/// Returns the 64-bit performance core bitmask.
#[no_mangle]
pub extern "C" fn nfs_perf_cores_mask() -> u64 {
    let cluster = CpuCluster::detect();
    cluster.perf_cores_mask
}

/// Returns the total number of CPU cores detected.
#[no_mangle]
pub extern "C" fn nfs_perf_cores_count() -> c_int {
    let cluster = CpuCluster::detect();
    cluster.total_cores as c_int
}

// ---- JNI Interface for io.mininxd.nfsu2.PerformanceCores ------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeBindToPerformanceCores(
    _env: JNIEnv,
    _class: JClass,
) -> jint {
    nfs_perf_cores_bind() as jint
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeGetPerformanceCoresMask(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    nfs_perf_cores_mask() as jlong
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeGetCoreCount(
    _env: JNIEnv,
    _class: JClass,
) -> jint {
    nfs_perf_cores_count() as jint
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeGetSummary(
    mut env: JNIEnv,
    _class: JClass,
) -> jstring {
    let cluster = CpuCluster::detect();
    let summary = format!(
        "Total Cores: {}, Perf Cores: {} (mask: 0x{:X})",
        cluster.total_cores, cluster.perf_core_count, cluster.perf_cores_mask
    );
    env.new_string(summary)
        .map(|s| s.into_raw())
        .unwrap_or(std::ptr::null_mut())
}
