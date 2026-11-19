//! Safe Rust thread role tuning and CPU core pinning for NFSU2 Android.
//! Replaces android/app/src/main/cpp/performance_tuner.cpp with safe system calls.

use std::ffi::CStr;
use std::os::raw::{c_char, c_int, c_long};
use crate::CpuCluster;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThreadRole {
    RenderMain = 0,
    GuestAudio = 1,
    BackgroundWorker = 2,
}

impl ThreadRole {
    pub fn from_i32(val: i32) -> Self {
        match val {
            0 => ThreadRole::RenderMain,
            1 => ThreadRole::GuestAudio,
            _ => ThreadRole::BackgroundWorker,
        }
    }
}

/// Pins thread affinity and sets process/thread priority according to operational role.
pub fn tune_current_thread(role: ThreadRole, name: Option<&str>) {
    let cluster = CpuCluster::detect();

    if let Some(tname) = name {
        if let Ok(cname) = std::ffi::CString::new(tname) {
            unsafe {
                libc::pthread_setname_np(libc::pthread_self(), cname.as_ptr());
            }
        }
    }

    let (mask, nice) = match role {
        ThreadRole::RenderMain => {
            // Main rendering thread: bind to prime + mid performance cluster, urgent nice (-16)
            (cluster.perf_cores_mask, -16)
        }
        ThreadRole::GuestAudio => {
            // Audio thread: isolate onto dedicated mid performance cores to avoid prime core contention
            let target = if cluster.mid_cores_mask != 0 && cluster.mid_cores_mask != cluster.prime_cores_mask {
                cluster.mid_cores_mask
            } else {
                cluster.perf_cores_mask
            };
            (target, -12)
        }
        ThreadRole::BackgroundWorker => {
            // Background workers / asset loading: allow little + mid cores, normal nice (0)
            let target = if cluster.little_cores_mask != 0 {
                cluster.little_cores_mask | cluster.mid_cores_mask
            } else {
                (1u64 << cluster.total_cores) - 1
            };
            (target, 0)
        }
    };

    unsafe {
        let mask_val = mask as c_long;
        let _ = libc::syscall(
            libc::SYS_sched_setaffinity,
            0 as c_int, // calling thread
            std::mem::size_of::<c_long>(),
            &mask_val as *const c_long,
        );
        let _ = libc::setpriority(libc::PRIO_PROCESS, 0, nice);
    }
}

// ---------------------------------------------------------------------------
// C ABI Compatibility Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_perf_tune_thread_role(role: c_int, name: *const c_char) {
    let name_str = if !name.is_null() {
        CStr::from_ptr(name).to_str().ok()
    } else {
        None
    };
    tune_current_thread(ThreadRole::from_i32(role), name_str);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_perf_get_topology_summary_buf(buf: *mut c_char, cap: usize) -> c_int {
    if buf.is_null() || cap == 0 {
        return -1;
    }
    let cluster = CpuCluster::detect();
    let summary = format!(
        "Total: {} | Prime: {} (0x{:X}) | Mid: {} (0x{:X}) | Little: {} (0x{:X})",
        cluster.total_cores,
        cluster.prime_core_count, cluster.prime_cores_mask,
        cluster.mid_core_count, cluster.mid_cores_mask,
        cluster.little_core_count, cluster.little_cores_mask
    );
    let bytes = summary.as_bytes();
    let copy_len = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, copy_len);
    *buf.add(copy_len) = 0;
    copy_len as c_int
}
