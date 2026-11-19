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
pub fn tune_current_thread(role: ThreadRole, _name: Option<&str>) {
    let cluster = CpuCluster::detect();

    let (mask, nice) = match role {
        ThreadRole::RenderMain => {
            // Main rendering thread: bind to prime/performance cores, high priority (-16)
            (cluster.perf_cores_mask, -16)
        }
        ThreadRole::GuestAudio => {
            // Audio thread: bind to performance cores, elevated priority (-10)
            (cluster.perf_cores_mask, -10)
        }
        ThreadRole::BackgroundWorker => {
            // Background compilation/workers: all cores or lower half, standard priority (0)
            let all_mask = if cluster.total_cores >= 64 {
                !0u64
            } else {
                (1u64 << cluster.total_cores) - 1
            };
            (all_mask, 0)
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
        "Total: {}, Perf: {} (Mask: 0x{:X})",
        cluster.total_cores, cluster.perf_core_count, cluster.perf_cores_mask
    );
    let bytes = summary.as_bytes();
    let copy_len = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, copy_len);
    *buf.add(copy_len) = 0;
    copy_len as c_int
}
