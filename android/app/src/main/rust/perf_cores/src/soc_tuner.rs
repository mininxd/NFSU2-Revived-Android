//! MediaTek Dimensity & Helio Specialized SoC Architecture Tuner
//!
//! MediaTek SoCs (Dimensity 9000/9200/9300/8000/7000/6000 and Helio G90/G95/G99/G100)
//! feature distinct heterogeneous clusters (DynamIQ):
//! - Tri-cluster: 1x Cortex-X Prime + 3x Cortex-A7xx Mid + 4x Cortex-A5xx Little
//! - Dual-cluster: 2x/4x Cortex-A76/A78/A77 Big + 4x/6x Cortex-A55 Little (Helio G99/G100)
//! - All-Big cluster: 4x Cortex-X4 + 4x Cortex-A720 (Dimensity 9300)
//!
//! Under Android Energy Aware Scheduling (EAS), single-threaded lifted x86 binaries
//! are placed by default on the energy-efficient Cortex-A55/A510 Little cluster.
//! Because Cortex-A55 is dual-issue in-order, it bottlenecks at 100% capacity (10-12% total CPU)
//! producing only 25-30 FPS while the Mali GPU sits starved at 20% utilization.
//!
//! This tuner detects Cortex-A7x/X Big cores directly across sysfs, /proc/cpuinfo part IDs,
//! and capacity tables, locking the render thread to high-throughput Big cores with 1ns timer slack.

use std::fs;
use std::os::raw::{c_int, c_long};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocFamily {
    MediatekDimensity,
    MediatekHelio,
    QualcommSnapdragon,
    GoogleTensor,
    GenericArm,
}

#[derive(Debug, Clone)]
pub struct SocInfo {
    pub family: SocFamily,
    pub chip_name: String,
    pub total_cores: usize,
    pub big_cores_mask: u64,
    pub little_cores_mask: u64,
    pub big_cores_count: usize,
}

impl SocInfo {
    pub fn detect() -> Self {
        let total_cores = Self::detect_total_cores();
        let mut big_cores_mask = 0u64;
        let mut little_cores_mask = 0u64;

        // 1. Parse /proc/cpuinfo for Hardware, Model, and ARM Cortex Part IDs
        let mut hardware_str = String::new();
        let mut cpuinfo_parts: Vec<(usize, u32)> = Vec::new();

        if let Ok(content) = fs::read_to_string("/proc/cpuinfo") {
            let mut cur_cpu = 0usize;
            for line in content.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with("processor") {
                    if let Some(colon) = trimmed.find(':') {
                        if let Ok(id) = trimmed[colon + 1..].trim().parse::<usize>() {
                            cur_cpu = id;
                        }
                    }
                } else if trimmed.starts_with("CPU part") {
                    if let Some(colon) = trimmed.find(':') {
                        let hex_str = trimmed[colon + 1..].trim().trim_start_matches("0x");
                        if let Ok(part) = u32::from_str_radix(hex_str, 16) {
                            cpuinfo_parts.push((cur_cpu, part));
                        }
                    }
                } else if trimmed.starts_with("Hardware") || trimmed.starts_with("model name") {
                    if let Some(colon) = trimmed.find(':') {
                        hardware_str = trimmed[colon + 1..].trim().to_string();
                    }
                }
            }
        }

        // 2. Identify Big vs Little cores from ARM Architecture and Qualcomm Oryon Part IDs
        let lower_hw = hardware_str.to_lowercase();
        let is_all_big_soc = lower_hw.contains("sm8750")
            || lower_hw.contains("sun")
            || lower_hw.contains("oryon")
            || lower_hw.contains("mt6989")
            || lower_hw.contains("mt6991")
            || lower_hw.contains("dimensity 9300")
            || lower_hw.contains("dimensity 9400");

        if is_all_big_soc {
            big_cores_mask = if total_cores >= 64 { u64::MAX } else { (1u64 << total_cores) - 1 };
            little_cores_mask = 0;
        } else {
            for &(cpu_id, part) in &cpuinfo_parts {
                if cpu_id < total_cores {
                    let is_little = part == 0xd03 || part == 0xd05 || part == 0xd46 || part == 0xd80;
                    let is_big = (part >= 0xd08 && !is_little) || part == 0xd44 || part == 0xd42
                        || part == 0xd48 || part == 0xd4f || part == 0xd85
                        || part == 0x001 || part == 0x002 || part == 0x005;

                    if is_big {
                        big_cores_mask |= 1u64 << cpu_id;
                    } else if is_little {
                        little_cores_mask |= 1u64 << cpu_id;
                    }
                }
            }
        }

        // 3. Fallback to CPU frequency sysfs if CPU part IDs were obscured by virtualization/SELinux
        if big_cores_mask == 0 {
            let mut max_freq = 0u64;
            let mut freqs = Vec::new();
            for i in 0..total_cores {
                let p = format!("/sys/devices/system/cpu/cpu{}/cpufreq/cpuinfo_max_freq", i);
                let f = fs::read_to_string(&p)
                    .ok()
                    .and_then(|s| s.trim().parse::<u64>().ok())
                    .unwrap_or(0);
                if f > max_freq {
                    max_freq = f;
                }
                freqs.push((i, f));
            }

            if max_freq > 0 {
                let threshold = (max_freq * 80) / 100;
                for (i, f) in freqs {
                    if f >= threshold {
                        big_cores_mask |= 1u64 << i;
                    } else {
                        little_cores_mask |= 1u64 << i;
                    }
                }
            }
        }

        // 4. Secondary fallback: CPU capacity sysfs
        if big_cores_mask == 0 {
            let mut max_cap = 0u64;
            let mut caps = Vec::new();
            for i in 0..total_cores {
                let p = format!("/sys/devices/system/cpu/cpu{}/cpu_capacity", i);
                let c = fs::read_to_string(&p)
                    .ok()
                    .and_then(|s| s.trim().parse::<u64>().ok())
                    .unwrap_or(0);
                if c > max_cap {
                    max_cap = c;
                }
                caps.push((i, c));
            }

            if max_cap > 500 {
                let threshold = (max_cap * 80) / 100;
                for (i, c) in caps {
                    if c >= threshold {
                        big_cores_mask |= 1u64 << i;
                    } else {
                        little_cores_mask |= 1u64 << i;
                    }
                }
            }
        }

        // 5. Final architectural fallback: on modern chips where sysfs is blocked, treat all cores as Big
        if big_cores_mask == 0 {
            big_cores_mask = if total_cores >= 64 { u64::MAX } else { (1u64 << total_cores) - 1 };
            little_cores_mask = 0;
        }

        // Determine SoC family
        let hw_lower = hardware_str.to_lowercase();
        let family = if hw_lower.contains("mt") || hw_lower.contains("dimensity") || hw_lower.contains("helio") {
            if hw_lower.contains("dimensity") || hw_lower.contains("mt68") || hw_lower.contains("mt69") {
                SocFamily::MediatekDimensity
            } else {
                SocFamily::MediatekHelio
            }
        } else if hw_lower.contains("qcom") || hw_lower.contains("sm") || hw_lower.contains("snapdragon") || hw_lower.contains("sun") || hw_lower.contains("oryon") {
            SocFamily::QualcommSnapdragon
        } else if hw_lower.contains("gs") || hw_lower.contains("tensor") {
            SocFamily::GoogleTensor
        } else {
            SocFamily::GenericArm
        };

        SocInfo {
            family,
            chip_name: hardware_str,
            total_cores,
            big_cores_mask,
            little_cores_mask,
            big_cores_count: big_cores_mask.count_ones() as usize,
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
        std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(8)
    }

    /// Enforces peak performance pinning on the calling render thread:
    /// - Locks affinity to the Big/Prime cores (Cortex-A76/A78/X)
    /// - Elevates thread priority to maximum interactive nice (-20)
    /// - Sets Linux timer slack to 1ns (eliminating kernel sleep jitter)
    pub fn enforce_render_thread_boost(&self) {
        unsafe {
            let mask = self.big_cores_mask as c_long;
            let _ = libc::syscall(
                libc::SYS_sched_setaffinity,
                0 as c_int,
                std::mem::size_of::<c_long>(),
                &mask as *const c_long,
            );
            let _ = libc::setpriority(libc::PRIO_PROCESS, 0, -20);

            // PR_SET_TIMERSLACK = 29
            const PR_SET_TIMERSLACK: c_int = 29;
            let _ = libc::prctl(PR_SET_TIMERSLACK, 1 as c_long, 0, 0, 0);
        }
    }
}

// -----------------------------------------------------------------------------
// C ABI Interface for direct invocation by C++ guest runtime
// -----------------------------------------------------------------------------

static SOC_INIT: std::sync::Once = std::sync::Once::new();
static mut GLOBAL_SOC_INFO: Option<SocInfo> = None;

#[no_mangle]
pub unsafe extern "C" fn nfs_soc_init() {
    SOC_INIT.call_once(|| {
        GLOBAL_SOC_INFO = Some(SocInfo::detect());
    });
}

#[no_mangle]
pub unsafe extern "C" fn nfs_soc_boost_current_render_thread() {
    nfs_soc_init();
    if let Some(ref soc) = GLOBAL_SOC_INFO {
        soc.enforce_render_thread_boost();
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_soc_get_big_cores_mask() -> u64 {
    nfs_soc_init();
    GLOBAL_SOC_INFO
        .as_ref()
        .map(|s| s.big_cores_mask)
        .unwrap_or(0xFF)
}
