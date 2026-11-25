//! CPU Performance Cores affinity detection and pinning for NFSU2 Android Launcher.
//!
//! Modern Android SoC chips (Snapdragon, Dimensity, Tensor, Exynos) employ big.LITTLE
//! or DynamIQ heterogeneous architectures containing:
//!  - LITTLE Cores (Efficiency / battery saver, e.g. Cortex-A55 / A510, ~1.8-2.0 GHz)
//!  - MID / BIG Cores (Performance, e.g. Cortex-A78 / A715 / A720, ~2.6-3.0 GHz)
//!  - PRIME Cores (Ultra-performance, e.g. Cortex-X1 / X3 / X4, ~3.2-3.4 GHz)
//!
//! If the process defaults to CPU 0 (a LITTLE efficiency core), or threads fight for
//! the prime core, the game suffers from frame stutter and audio underruns.
//! This module identifies the tri-cluster CPU topology and applies intelligent
//! thread pinning so the guest emulation, Vulkan presentation, and audio pipeline
//! run on their optimal cores.

pub mod adpf;
pub mod steering_pacer;
pub mod tuner;
pub mod logger;
pub mod soc_tuner;
pub mod diagnostics;
pub mod game_manager;
pub mod gpu_governor;
pub use adpf::*;
pub use steering_pacer::*;
pub use tuner::*;
pub use logger::*;
pub use soc_tuner::*;
pub use diagnostics::*;
pub use game_manager::*;
pub use gpu_governor::*;

use std::fs;
use std::os::raw::{c_int, c_long};
use jni::JNIEnv;
use jni::objects::{JClass, JString};
use jni::sys::{jint, jlong, jstring};

/// Cluster hierarchy classification for CPU cores.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CoreClusterRole {
    Prime = 0,
    Mid = 1,
    Little = 2,
}

/// Represents information about a single CPU core.
#[derive(Debug, Clone, Copy)]
pub struct CoreInfo {
    pub cpu_id: usize,
    pub max_freq_khz: u64,
    pub capacity: u64,
    pub role: CoreClusterRole,
    pub is_performance: bool,
}

/// Topology detection result with tri-cluster core masks.
#[derive(Debug, Clone)]
pub struct CpuCluster {
    pub total_cores: usize,
    pub prime_cores_mask: u64,
    pub mid_cores_mask: u64,
    pub little_cores_mask: u64,
    pub perf_cores_mask: u64, // prime | mid
    pub prime_core_count: usize,
    pub mid_core_count: usize,
    pub little_core_count: usize,
    pub perf_core_count: usize,
    pub cores: Vec<CoreInfo>,
}

impl CpuCluster {
    /// Detects the device CPU topology by inspecting sysfs cpufreq nodes and /proc/cpuinfo.
    pub fn detect() -> Self {
        let total_cores = Self::detect_total_cores();
        let mut raw_cores: Vec<(usize, u64, u64, Option<CoreClusterRole>)> = Vec::with_capacity(total_cores);
        let mut highest_freq: u64 = 0;
        let mut lowest_freq: u64 = u64::MAX;

        for id in 0..total_cores {
            let freq_path = format!("/sys/devices/system/cpu/cpu{}/cpufreq/cpuinfo_max_freq", id);
            let scaling_path = format!("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_max_freq", id);
            let capacity_path = format!("/sys/devices/system/cpu/cpu{}/cpu_capacity", id);

            let mut freq = fs::read_to_string(&freq_path)
                .ok()
                .and_then(|s| s.trim().parse::<u64>().ok())
                .unwrap_or(0);

            if freq == 0 {
                freq = fs::read_to_string(&scaling_path)
                    .ok()
                    .and_then(|s| s.trim().parse::<u64>().ok())
                    .unwrap_or(0);
            }

            let mut cap = fs::read_to_string(&capacity_path)
                .ok()
                .and_then(|s| s.trim().parse::<u64>().ok())
                .unwrap_or(0);

            if freq == 0 && cap > 0 {
                freq = cap * 1000;
            }

            if freq > highest_freq {
                highest_freq = freq;
            }
            if freq > 0 && freq < lowest_freq {
                lowest_freq = freq;
            }

            raw_cores.push((id, freq, cap, None));
        }

        if lowest_freq == u64::MAX {
            lowest_freq = 0;
        }

        // Secondary detection: Parse /proc/cpuinfo for known ARM Cortex and Qualcomm Oryon part numbers
        let mut is_all_big_soc = false;
        if let Ok(cpuinfo) = fs::read_to_string("/proc/cpuinfo") {
            let mut cur_cpu: i32 = -1;
            let mut is_qualcomm = false;
            let mut qcom_cores = 0usize;
            let mut arm_little_cores = 0usize;
            for line in cpuinfo.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with("processor") {
                    if let Some(colon) = trimmed.find(':') {
                        if let Ok(cpu_id) = trimmed[colon + 1..].trim().parse::<i32>() {
                            cur_cpu = cpu_id;
                        }
                    }
                } else if trimmed.starts_with("CPU implementer") {
                    if let Some(colon) = trimmed.find(':') {
                        let impl_str = trimmed[colon + 1..].trim();
                        if let Ok(imp) = u32::from_str_radix(impl_str.trim_start_matches("0x"), 16) {
                            if imp == 0x51 {
                                is_qualcomm = true;
                                qcom_cores += 1;
                            }
                        }
                    }
                } else if trimmed.starts_with("Hardware") || trimmed.starts_with("model name") {
                    let lower = trimmed.to_lowercase();
                    if lower.contains("sm8750") || lower.contains("sm8850") || lower.contains("sm8845")
                        || lower.contains("sun") || lower.contains("canoe") || lower.contains("oryon")
                        || lower.contains("mt6989") || lower.contains("mt6991") || lower.contains("mt6993")
                        || lower.contains("dimensity 9300") || lower.contains("dimensity 9400")
                        || lower.contains("dimensity 9500")
                    {
                        is_all_big_soc = true;
                    }
                } else if trimmed.starts_with("CPU part") && cur_cpu >= 0 && (cur_cpu as usize) < total_cores {
                    if let Some(colon) = trimmed.find(':') {
                        let part_str = trimmed[colon + 1..].trim();
                        let part = u32::from_str_radix(part_str.trim_start_matches("0x"), 16).unwrap_or(0);
                        if is_qualcomm && (part == 0x001 || part == 0x002 || part == 0x003 || part == 0x005 || (part >= 0x800 && part <= 0x8ff)) {
                            is_all_big_soc = true;
                        }
                        let role = match part {
                            // Prime / Ultra-high performance cores (Cortex-X1/X2/X3/X4, Cortex-X925)
                            0xd44 | 0xd48 | 0xd4e | 0xd4f | 0xd82 => Some(CoreClusterRole::Prime),
                            // Performance / Mid cores (Cortex-A7x series)
                            0xd07 | 0xd08 | 0xd09 | 0xd0a | 0xd0b | 0xd0d | 0xd41 | 0xd47 | 0xd4d | 0xd81 | 0xd87 => {
                                Some(CoreClusterRole::Mid)
                            }
                            // Efficiency / Little cores (Cortex-A5x series)
                            0xd03 | 0xd05 | 0xd46 | 0xd80 => {
                                arm_little_cores += 1;
                                Some(CoreClusterRole::Little)
                            }
                            _ => None,
                        };
                        if let Some(r) = role {
                            if let Some(item) = raw_cores.get_mut(cur_cpu as usize) {
                                item.3 = Some(r);
                            }
                        }
                    }
                }
            }
            if qcom_cores >= total_cores && total_cores >= 8 && arm_little_cores == 0 {
                is_all_big_soc = true;
            }
        }

        // Tri-cluster frequency / capacity clustering
        let mut final_cores: Vec<CoreInfo> = Vec::with_capacity(total_cores);
        let mut prime_cores_mask: u64 = 0;
        let mut mid_cores_mask: u64 = 0;
        let mut little_cores_mask: u64 = 0;

        for (id, freq, cap, explicit_role) in raw_cores {
            let role = if is_all_big_soc {
                // All-big SoC (Snapdragon 8 Elite SM8750 Oryon, Dimensity 9300/9400):
                // All cores are high-performance cores, zero Little efficiency cores!
                // Top 2 cores are Prime (e.g. 4.32GHz), others are Mid/Performance (e.g. 3.53GHz).
                if id >= total_cores.saturating_sub(2) {
                    CoreClusterRole::Prime
                } else {
                    CoreClusterRole::Mid
                }
            } else if let Some(r) = explicit_role {
                r
            } else if highest_freq > 0 && lowest_freq > 0 && highest_freq > lowest_freq {
                let prime_thresh = (highest_freq as f64 * 0.92) as u64;
                let mid_thresh = (highest_freq as f64 * 0.65) as u64;
                if freq >= prime_thresh {
                    CoreClusterRole::Prime
                } else if freq >= mid_thresh {
                    CoreClusterRole::Mid
                } else {
                    CoreClusterRole::Little
                }
            } else if cap > 0 {
                if cap >= 950 {
                    CoreClusterRole::Prime
                } else if cap >= 500 {
                    CoreClusterRole::Mid
                } else {
                    CoreClusterRole::Little
                }
            } else {
                // Architectural fallback when sysfs is restricted by SELinux:
                // Treat all available cores as performance-capable (top 25% Prime, remainder Mid)
                // to prevent starving the guest engine of CPU cores on unrecognized modern chips.
                let ratio = (id as f64) / (total_cores as f64);
                if ratio >= 0.75 {
                    CoreClusterRole::Prime
                } else {
                    CoreClusterRole::Mid
                }
            };

            let is_perf = role != CoreClusterRole::Little;
            match role {
                CoreClusterRole::Prime => prime_cores_mask |= 1u64 << id,
                CoreClusterRole::Mid => mid_cores_mask |= 1u64 << id,
                CoreClusterRole::Little => little_cores_mask |= 1u64 << id,
            }

            final_cores.push(CoreInfo {
                cpu_id: id,
                max_freq_khz: freq,
                capacity: cap,
                role,
                is_performance: is_perf,
            });
        }

        let all_cores_mask = if total_cores >= 64 {
            u64::MAX
        } else {
            (1u64 << total_cores) - 1
        };

        let mut perf_cores_mask = prime_cores_mask | mid_cores_mask;
        if perf_cores_mask == 0 {
            perf_cores_mask = all_cores_mask;
        }
        if prime_cores_mask == 0 {
            prime_cores_mask = perf_cores_mask;
        }
        if mid_cores_mask == 0 {
            mid_cores_mask = perf_cores_mask;
        }

        let prime_core_count = prime_cores_mask.count_ones() as usize;
        let mid_core_count = mid_cores_mask.count_ones() as usize;
        let little_core_count = little_cores_mask.count_ones() as usize;
        let perf_core_count = perf_cores_mask.count_ones() as usize;

        CpuCluster {
            total_cores,
            prime_cores_mask,
            mid_cores_mask,
            little_cores_mask,
            perf_cores_mask,
            prime_core_count,
            mid_core_count,
            little_core_count,
            perf_core_count,
            cores: final_cores,
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

    /// Intelligently assigns affinity and priorities to process and threads in /proc/self/task.
    /// Uses all available CPU cores so that no cores idle and the kernel governor can boost clock frequencies.
    pub fn apply_affinity(&self) -> Result<(), i32> {
        let all_cores_mask = if self.total_cores >= 64 {
            u64::MAX
        } else {
            (1u64 << self.total_cores) - 1
        };
        let perf_mask = if self.perf_cores_mask != 0 {
            self.perf_cores_mask
        } else {
            all_cores_mask
        };
        let prime_mask = if self.prime_cores_mask != 0 {
            self.prime_cores_mask
        } else {
            perf_mask
        };
        let mid_mask = if self.mid_cores_mask != 0 {
            self.mid_cores_mask
        } else {
            perf_mask
        };
        // On tri-cluster / all-big >=6 perf-core SoCs (e.g. Snapdragon 8 Elite / SM8850),
        // dedicate Prime cores to the single-threaded GuestBootWorker while letting
        // DXVK/Vulkan/UI threads run across Mid cores without preempting the emulator.
        // On 2-core big clusters (e.g. Dimensity 900 MT6877V 2xA78+6xA55), pin only
        // GuestBootWorker to perf_mask and leave DXVK/background threads on all cores so
        // 15+ threads do not contend on 2 big cores.
        let dedicated_prime = self.perf_core_count >= 6 && self.prime_core_count >= 2 && self.mid_core_count >= 4;
        let emu_mask = if dedicated_prime { prime_mask } else { perf_mask };
        let worker_mask = if dedicated_prime {
            mid_mask
        } else if self.perf_core_count >= 4 {
            perf_mask
        } else {
            all_cores_mask
        };

        unsafe {
            let all_mask_val = all_cores_mask as c_long;
            let _ = libc::syscall(
                libc::SYS_sched_setaffinity,
                libc::getpid() as c_int,
                std::mem::size_of::<c_long>(),
                &all_mask_val as *const c_long,
            );
            let caller_mask = if self.perf_core_count >= 4 {
                worker_mask as c_long
            } else {
                all_mask_val
            };
            let ret = libc::syscall(
                libc::SYS_sched_setaffinity,
                0 as c_int,
                std::mem::size_of::<c_long>(),
                &caller_mask as *const c_long,
            );
            let _ = libc::setpriority(libc::PRIO_PROCESS, libc::getpid() as u32, -10);

            if let Ok(entries) = fs::read_dir("/proc/self/task") {
                for entry in entries.flatten() {
                    if let Ok(tid) = entry.file_name().to_string_lossy().parse::<i32>() {
                        let comm_path = format!("/proc/self/task/{}/comm", tid);
                        let comm = fs::read_to_string(&comm_path).unwrap_or_default();
                        let n = comm.trim();

                        let is_audio = n.starts_with("AAudio") || n.contains("audio") || n.contains("Audio");
                        let is_guest_main = n == "GuestBootWorker";
                        let is_dxvk_or_render = n.starts_with("dxvk-") || n == "RenderThread";

                        let (m, prio) = if is_guest_main {
                            (emu_mask as c_long, -20)
                        } else if is_audio {
                            (all_cores_mask as c_long, -16)
                        } else if is_dxvk_or_render {
                            (worker_mask as c_long, -14)
                        } else {
                            (all_cores_mask as c_long, 0)
                        };

                        let _ = libc::syscall(
                            libc::SYS_sched_setaffinity,
                            tid as c_int,
                            std::mem::size_of::<c_long>(),
                            &m as *const c_long,
                        );
                        let _ = libc::setpriority(libc::PRIO_PROCESS, tid as u32, prio);
                    }
                }
            }

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

/// Compatibility export matching roam_affinity.h (replaces C roam_affinity.c)
#[no_mangle]
pub extern "C" fn nfs_bind_to_perf_cores() -> c_int {
    let cluster = CpuCluster::detect();
    let _ = cluster.apply_affinity();
    cluster.perf_core_count as c_int
}

/// Binds the calling process to performance cores. Returns 0 on success, or -1 on error.
#[no_mangle]
pub extern "C" fn nfs_perf_cores_bind() -> c_int {
    let cluster = CpuCluster::detect();
    match cluster.apply_affinity() {
        Ok(_) => 0,
        Err(_) => -1,
    }
}

/// Returns the 64-bit performance core bitmask (Prime | Mid).
#[no_mangle]
pub extern "C" fn nfs_perf_cores_mask() -> u64 {
    let cluster = CpuCluster::detect();
    cluster.perf_cores_mask
}

/// Returns the 64-bit Prime core bitmask.
#[no_mangle]
pub extern "C" fn nfs_perf_prime_cores_mask() -> u64 {
    let cluster = CpuCluster::detect();
    cluster.prime_cores_mask
}

/// Returns the 64-bit Mid core bitmask.
#[no_mangle]
pub extern "C" fn nfs_perf_mid_cores_mask() -> u64 {
    let cluster = CpuCluster::detect();
    cluster.mid_cores_mask
}

/// Returns the 64-bit Little core bitmask.
#[no_mangle]
pub extern "C" fn nfs_perf_little_cores_mask() -> u64 {
    let cluster = CpuCluster::detect();
    cluster.little_cores_mask
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
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeGetPrimeCoresMask(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    nfs_perf_prime_cores_mask() as jlong
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeGetMidCoresMask(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    nfs_perf_mid_cores_mask() as jlong
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeGetLittleCoresMask(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    nfs_perf_little_cores_mask() as jlong
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
        "Cores: {} | Prime: 0x{:X} | Mid: 0x{:X} | Little: 0x{:X}",
        cluster.total_cores, cluster.prime_cores_mask, cluster.mid_cores_mask, cluster.little_cores_mask
    );
    env.new_string(summary)
        .map(|s| s.into_raw())
        .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeBoostCallingThread(
    _env: JNIEnv,
    _class: JClass,
) {
    let cluster = CpuCluster::detect();
    let mask_val = cluster.perf_cores_mask as c_long;
    let _ = libc::syscall(
        libc::SYS_sched_setaffinity,
        0 as c_int,
        std::mem::size_of::<c_long>(),
        &mask_val as *const c_long,
    );
    let _ = libc::setpriority(libc::PRIO_PROCESS, 0, -16);
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_PerformanceCores_nativeCalculateSteering(
    _env: JNIEnv,
    _class: JClass,
    raw_x: jni::sys::jfloat,
    center_x: jni::sys::jfloat,
    half_width: jni::sys::jfloat,
    scale: jni::sys::jfloat,
) -> jni::sys::jfloat {
    if scale <= 0.0 || half_width <= 0.0 {
        return 0.0;
    }
    let norm = (raw_x / scale - center_x) / half_width;
    let clamped = norm.clamp(-1.0, 1.0);
    // Progressive steering curve: 30% linear + 70% cubic for highway stability & sharp lock
    let abs_val = clamped.abs();
    let curved = abs_val * 0.30 + abs_val * abs_val * abs_val * 0.70;
    if clamped < 0.0 { -curved } else { curved }
}

// ---- JNI Interface for io.mininxd.nfsu2.GamePerformanceBooster -----------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_GamePerformanceBooster_nativeApplyBooster(
    mut env: JNIEnv,
    _class: JClass,
    _activity: jni::objects::JObject,
) {
    // 1. Elevate process and calling thread priority to real-time / urgent display (-16)
    let _ = libc::setpriority(libc::PRIO_PROCESS, 0, -16);
    let _ = libc::setpriority(libc::PRIO_PROCESS, libc::getpid() as u32, -16);

    // 2. Bind process and all threads in /proc/self/task to performance CPU cluster
    let cluster = CpuCluster::detect();
    let _ = cluster.apply_affinity();

    // 2b. Start GPU Governor Keeper to lock peak GPU clock and prevent race/explore downclocking
    start_gpu_governor_keeper();

    // 3. Platform-specific OEM booster coordination (Xiaomi HyperOS / Joyose, MediaTek)
    if let Ok(perf_class) = env.find_class("android/os/PerfService") {
        if let Ok(method_id) = env.get_static_method_id(&perf_class, "boostEnable", "(I)V") {
            let _ = env.call_static_method_unchecked(
                &perf_class,
                method_id,
                jni::signature::ReturnType::Primitive(jni::signature::Primitive::Void),
                &[jni::sys::jvalue { i: 1 }],
            );
        }
    }
    let _ = env.exception_clear();

    if let Ok(joyose_class) = env.find_class("com/xiaomi/joyose/JoyoseManager") {
        if let Ok(get_method) = env.get_static_method_id(&joyose_class, "getInstance", "()Lcom/xiaomi/joyose/JoyoseManager;") {
            if let Ok(instance_val) = env.call_static_method_unchecked(
                &joyose_class,
                get_method,
                jni::signature::ReturnType::Object,
                &[],
            ) {
                if let Ok(instance_obj) = instance_val.l() {
                    if !instance_obj.is_null() {
                        if let Ok(set_game_method) = env.get_method_id(&joyose_class, "setGameMode", "(Z)V") {
                            let _ = env.call_method_unchecked(
                                &instance_obj,
                                set_game_method,
                                jni::signature::ReturnType::Primitive(jni::signature::Primitive::Void),
                                &[jni::sys::jvalue { z: 1 }],
                            );
                        }
                    }
                }
            }
        }
    }
    let _ = env.exception_clear();

    if let Ok(mtk_class) = env.find_class("com/mediatek/perfservice/PerfServiceWrapper") {
        if let Ok(method_id) = env.get_static_method_id(&mtk_class, "boostEnable", "(I)V") {
            let _ = env.call_static_method_unchecked(
                &mtk_class,
                method_id,
                jni::signature::ReturnType::Primitive(jni::signature::Primitive::Void),
                &[jni::sys::jvalue { i: 1 }],
            );
        }
    }
    let _ = env.exception_clear();
}


