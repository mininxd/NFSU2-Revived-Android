//! All CPU Cores Enforcer for NFSU2 Android.
//!
//! Ensures the entire game process, emulated guest threads, Vulkan/DXVK pipeline
//! workers, audio engine, and driver threads utilize ALL available CPU cores
//! (cores 0 through N-1) on the device without being restricted to a subset of cores.

use std::fs;
use std::os::raw::{c_int, c_long};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use jni::JNIEnv;
use jni::objects::JClass;
use jni::sys::{jint, jlong};

static SUPERVISOR_RUNNING: AtomicBool = AtomicBool::new(false);
static TOTAL_CORES_CACHE: AtomicU64 = AtomicU64::new(0);

/// Detects the total number of physical/logical CPU cores on the system.
pub fn detect_total_cpu_cores() -> usize {
    let cached = TOTAL_CORES_CACHE.load(Ordering::Relaxed);
    if cached > 0 {
        return cached as usize;
    }

    let mut total: usize = 0;

    // 1. Try reading /sys/devices/system/cpu/possible (e.g. "0-7" -> 8 cores)
    if let Ok(content) = fs::read_to_string("/sys/devices/system/cpu/possible") {
        let trimmed = content.trim();
        if let Some(pos) = trimmed.rfind('-') {
            if let Ok(end) = trimmed[pos + 1..].parse::<usize>() {
                total = end + 1;
            }
        }
    }

    // 2. Fallback to /sys/devices/system/cpu/present
    if total == 0 {
        if let Ok(content) = fs::read_to_string("/sys/devices/system/cpu/present") {
            let trimmed = content.trim();
            if let Some(pos) = trimmed.rfind('-') {
                if let Ok(end) = trimmed[pos + 1..].parse::<usize>() {
                    total = end + 1;
                }
            }
        }
    }

    // 3. Fallback to libc sysconf
    if total == 0 {
        let count = unsafe { libc::sysconf(libc::_SC_NPROCESSORS_CONF) };
        if count > 0 {
            total = count as usize;
        }
    }

    if total == 0 {
        total = 8; // Default octa-core
    }

    TOTAL_CORES_CACHE.store(total as u64, Ordering::Relaxed);
    total
}

/// Computes the 64-bit affinity bitmask representing ALL CPU cores on the device.
pub fn get_all_cores_bitmask() -> u64 {
    let total = detect_total_cpu_cores().min(64);
    if total >= 64 {
        !0u64
    } else {
        (1u64 << total) - 1
    }
}

/// Applies the ALL CPU CORES affinity mask to a specific thread ID or the current thread (tid = 0).
pub fn apply_all_cores_to_tid(tid: i32) -> Result<(), i32> {
    let mask_val = get_all_cores_bitmask() as c_long;
    unsafe {
        let ret = libc::syscall(
            libc::SYS_sched_setaffinity,
            tid as c_int,
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

/// Iterates through all active threads in /proc/self/task and binds every thread
/// to ALL CPU cores, elevating thread priority.
pub fn bind_all_threads_to_all_cores() {
    let all_mask = get_all_cores_bitmask() as c_long;

    // Apply to process PID and calling thread
    unsafe {
        let _ = libc::syscall(
            libc::SYS_sched_setaffinity,
            libc::getpid() as c_int,
            std::mem::size_of::<c_long>(),
            &all_mask as *const c_long,
        );
        let _ = libc::syscall(
            libc::SYS_sched_setaffinity,
            0 as c_int,
            std::mem::size_of::<c_long>(),
            &all_mask as *const c_long,
        );
        // Elevate base process priority to keep all cores active
        let _ = libc::setpriority(libc::PRIO_PROCESS, libc::getpid() as u32, -10);
        let _ = libc::setpriority(libc::PRIO_PROCESS, 0, -10);
    }

    // Apply to every thread listed in /proc/self/task
    if let Ok(entries) = fs::read_dir("/proc/self/task") {
        for entry in entries.flatten() {
            if let Ok(tid) = entry.file_name().to_string_lossy().parse::<i32>() {
                unsafe {
                    let _ = libc::syscall(
                        libc::SYS_sched_setaffinity,
                        tid as c_int,
                        std::mem::size_of::<c_long>(),
                        &all_mask as *const c_long,
                    );
                }

                // Elevate priority for critical gaming / rendering / emulation threads
                let comm_path = format!("/proc/self/task/{}/comm", tid);
                if let Ok(comm) = fs::read_to_string(&comm_path) {
                    let n = comm.trim();
                    if n == "GuestBootWorker"
                        || n == "RenderThread"
                        || n == "main"
                        || n.starts_with("AAudio")
                        || n.contains("audio")
                        || n.contains("nfs")
                        || n.contains("GfxAsync")
                        || n.contains("dxvk")
                    {
                        unsafe {
                            let _ = libc::setpriority(libc::PRIO_PROCESS, tid as u32, -10);
                        }
                    }
                }
            }
        }
    }
}

/// Starts a background supervisor daemon thread that continuously ensures any newly
/// created threads (Vulkan driver workers, guest worker, audio, etc.) use ALL CPU cores.
pub fn start_all_cores_supervisor() {
    if SUPERVISOR_RUNNING.swap(true, Ordering::SeqCst) {
        return; // Already running
    }

    // Immediately bind all existing threads
    bind_all_threads_to_all_cores();

    thread::Builder::new()
        .name("NfsAllCoresDaemon".to_string())
        .spawn(move || {
            while SUPERVISOR_RUNNING.load(Ordering::Relaxed) {
                bind_all_threads_to_all_cores();
                thread::sleep(Duration::from_millis(1000));
            }
        })
        .expect("Failed to spawn AllCores supervisor daemon");
}

// ---- C ABI Exports ---------------------------------------------------------------------------------

/// Binds the calling process and all threads to ALL CPU cores. Returns 0 on success.
#[no_mangle]
pub extern "C" fn nfs_use_all_cpu_cores() -> c_int {
    start_all_cores_supervisor();
    0
}

/// Returns the 64-bit bitmask for ALL CPU cores.
#[no_mangle]
pub extern "C" fn nfs_get_all_cpu_cores_mask() -> u64 {
    get_all_cores_bitmask()
}

/// Returns the total CPU core count.
#[no_mangle]
pub extern "C" fn nfs_get_total_cpu_cores_count() -> c_int {
    detect_total_cpu_cores() as c_int
}

// ---- JNI Interface for io.mininxd.nfsu2.AllCpuCoresManager ----------------------------------------

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_AllCpuCoresManager_nativeEnableAllCores(
    _env: JNIEnv,
    _class: JClass,
) -> jint {
    bind_all_threads_to_all_cores();
    nfs_use_all_cpu_cores() as jint
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_AllCpuCoresManager_nativeGetAllCoresMask(
    _env: JNIEnv,
    _class: JClass,
) -> jlong {
    nfs_get_all_cpu_cores_mask() as jlong
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_AllCpuCoresManager_nativeGetTotalCoreCount(
    _env: JNIEnv,
    _class: JClass,
) -> jint {
    nfs_get_total_cpu_cores_count() as jint
}
