//! Safe Rust implementation of Android Dynamic Performance Framework (ADPF)
//! hint sessions for NFSU2 Android.
//! Communicates frame work duration directly to the Android kernel governor and PowerHAL
//! (Android 12+ / API 31+), preventing thermal throttling dips and micro-stuttering.

use std::os::raw::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicPtr, Ordering};
use std::sync::Mutex;

type APerformanceHintManager = c_void;
type APerformanceHintSession = c_void;

type PfnGetManager = unsafe extern "C" fn() -> *mut APerformanceHintManager;
type PfnCreateSession = unsafe extern "C" fn(
    *mut APerformanceHintManager,
    *const i32,
    usize,
    i64,
) -> *mut APerformanceHintSession;
type PfnUpdateTarget = unsafe extern "C" fn(*mut APerformanceHintSession, i64);
type PfnReportActual = unsafe extern "C" fn(*mut APerformanceHintSession, i64);
type PfnCloseSession = unsafe extern "C" fn(*mut APerformanceHintSession);

// API 34+ unified CPU + GPU types
type AWorkDuration = c_void;
type PfnWorkCreate = unsafe extern "C" fn() -> *mut AWorkDuration;
type PfnWorkRelease = unsafe extern "C" fn(*mut AWorkDuration);
type PfnWorkSetStart = unsafe extern "C" fn(*mut AWorkDuration, i64);
type PfnWorkSetTotal = unsafe extern "C" fn(*mut AWorkDuration, i64);
type PfnWorkSetCpu = unsafe extern "C" fn(*mut AWorkDuration, i64);
type PfnWorkSetGpu = unsafe extern "C" fn(*mut AWorkDuration, i64);
type PfnReportActual2 = unsafe extern "C" fn(*mut APerformanceHintSession, *mut AWorkDuration) -> c_int;

struct AdpfApi {
    get_manager: PfnGetManager,
    create_session: PfnCreateSession,
    update_target: PfnUpdateTarget,
    report_actual: PfnReportActual,
    close_session: PfnCloseSession,
    work_create: Option<PfnWorkCreate>,
    work_release: Option<PfnWorkRelease>,
    work_set_start: Option<PfnWorkSetStart>,
    work_set_total: Option<PfnWorkSetTotal>,
    work_set_cpu: Option<PfnWorkSetCpu>,
    work_set_gpu: Option<PfnWorkSetGpu>,
    report_actual2: Option<PfnReportActual2>,
}

static ADPF_INITIALIZED: AtomicBool = AtomicBool::new(false);
static ADPF_SUPPORTED: AtomicBool = AtomicBool::new(false);
static ADPF_MANAGER: AtomicPtr<APerformanceHintManager> = AtomicPtr::new(std::ptr::null_mut());
static ADPF_SESSION: AtomicPtr<APerformanceHintSession> = AtomicPtr::new(std::ptr::null_mut());
static TARGET_DURATION_NS: AtomicI64 = AtomicI64::new(16_666_666); // Default 60 FPS (16.6ms)
static FRAME_START_NS: AtomicI64 = AtomicI64::new(0);

static API: Mutex<Option<AdpfApi>> = Mutex::new(None);

#[inline]
fn get_time_ns() -> i64 {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    unsafe {
        libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts);
    }
    (ts.tv_sec as i64) * 1_000_000_000 + (ts.tv_nsec as i64)
}

fn collect_all_tids() -> Vec<i32> {
    let mut tids = Vec::new();
    if let Ok(entries) = std::fs::read_dir("/proc/self/task") {
        for entry in entries.flatten() {
            if let Ok(tid) = entry.file_name().to_string_lossy().parse::<i32>() {
                tids.push(tid);
            }
        }
    }
    if tids.is_empty() {
        unsafe {
            tids.push(libc::syscall(libc::SYS_gettid) as i32);
        }
    }
    tids
}

fn load_adpf_symbols() {
    if ADPF_INITIALIZED.swap(true, Ordering::SeqCst) {
        return;
    }

    unsafe {
        let lib = libc::dlopen(b"libandroid.so\0".as_ptr() as *const c_char, libc::RTLD_NOW | libc::RTLD_LOCAL);
        if lib.is_null() {
            return;
        }

        let p_get = libc::dlsym(lib, b"APerformanceHint_getManager\0".as_ptr() as *const c_char);
        let p_create = libc::dlsym(lib, b"APerformanceHint_createSession\0".as_ptr() as *const c_char);
        let p_update = libc::dlsym(lib, b"APerformanceHint_updateTargetWorkDuration\0".as_ptr() as *const c_char);
        let p_report = libc::dlsym(lib, b"APerformanceHint_reportActualWorkDuration\0".as_ptr() as *const c_char);
        let p_close = libc::dlsym(lib, b"APerformanceHint_closeSession\0".as_ptr() as *const c_char);

        let p_work_create = libc::dlsym(lib, b"AWorkDuration_create\0".as_ptr() as *const c_char);
        let p_work_release = libc::dlsym(lib, b"AWorkDuration_release\0".as_ptr() as *const c_char);
        let p_work_set_start = libc::dlsym(lib, b"AWorkDuration_setWorkPeriodStartTimestampNanos\0".as_ptr() as *const c_char);
        let p_work_set_total = libc::dlsym(lib, b"AWorkDuration_setActualTotalDurationNanos\0".as_ptr() as *const c_char);
        let p_work_set_cpu = libc::dlsym(lib, b"AWorkDuration_setActualCpuDurationNanos\0".as_ptr() as *const c_char);
        let p_work_set_gpu = libc::dlsym(lib, b"AWorkDuration_setActualGpuDurationNanos\0".as_ptr() as *const c_char);
        let p_report_actual2 = libc::dlsym(lib, b"APerformanceHint_reportActualWorkDuration2\0".as_ptr() as *const c_char);

        if !p_get.is_null() && !p_create.is_null() && !p_update.is_null() && !p_report.is_null() && !p_close.is_null() {
            let api = AdpfApi {
                get_manager: std::mem::transmute(p_get),
                create_session: std::mem::transmute(p_create),
                update_target: std::mem::transmute(p_update),
                report_actual: std::mem::transmute(p_report),
                close_session: std::mem::transmute(p_close),
                work_create: if !p_work_create.is_null() { Some(std::mem::transmute(p_work_create)) } else { None },
                work_release: if !p_work_release.is_null() { Some(std::mem::transmute(p_work_release)) } else { None },
                work_set_start: if !p_work_set_start.is_null() { Some(std::mem::transmute(p_work_set_start)) } else { None },
                work_set_total: if !p_work_set_total.is_null() { Some(std::mem::transmute(p_work_set_total)) } else { None },
                work_set_cpu: if !p_work_set_cpu.is_null() { Some(std::mem::transmute(p_work_set_cpu)) } else { None },
                work_set_gpu: if !p_work_set_gpu.is_null() { Some(std::mem::transmute(p_work_set_gpu)) } else { None },
                report_actual2: if !p_report_actual2.is_null() { Some(std::mem::transmute(p_report_actual2)) } else { None },
            };

            let mgr = (api.get_manager)();
            if !mgr.is_null() {
                ADPF_MANAGER.store(mgr, Ordering::SeqCst);
                ADPF_SUPPORTED.store(true, Ordering::SeqCst);
                let mut guard = API.lock().unwrap();
                *guard = Some(api);
            }
        }
    }
}

pub fn adpf_init(target_fps: i32) {
    load_adpf_symbols();
    if !ADPF_SUPPORTED.load(Ordering::Relaxed) {
        return;
    }

    let fps = if target_fps <= 0 { 60 } else { target_fps };
    let target_ns = 1_000_000_000i64 / (fps as i64);
    TARGET_DURATION_NS.store(target_ns, Ordering::Relaxed);

    adpf_destroy();

    let guard = API.lock().unwrap();
    if let Some(ref api) = *guard {
        let mgr = ADPF_MANAGER.load(Ordering::Relaxed);
        if !mgr.is_null() {
            unsafe {
                let tids = collect_all_tids();
                let session = (api.create_session)(mgr, tids.as_ptr(), tids.len(), target_ns);
                if !session.is_null() {
                    ADPF_SESSION.store(session, Ordering::SeqCst);
                }
            }
        }
    }
}

pub fn adpf_begin_frame() {
    FRAME_START_NS.store(get_time_ns(), Ordering::Relaxed);
}

pub fn adpf_end_frame() {
    let session = ADPF_SESSION.load(Ordering::Relaxed);
    if session.is_null() {
        return;
    }

    let start_ns = FRAME_START_NS.load(Ordering::Relaxed);
    if start_ns == 0 {
        return;
    }

    let now_ns = get_time_ns();
    let actual_duration_ns = (now_ns - start_ns).max(1);
    let target_ns = TARGET_DURATION_NS.load(Ordering::Relaxed);

    // Force governor to boost CPU and GPU clocks:
    // If work duration is low, the governor may try to downclock to save power.
    // Reporting work load at or above target budget signals urgency and forces high frequency scaling!
    let report_total_ns = if actual_duration_ns < target_ns {
        target_ns + 500_000
    } else {
        actual_duration_ns
    };

    let guard = API.lock().unwrap();
    if let Some(ref api) = *guard {
        unsafe {
            if let (Some(work_create), Some(work_release), Some(report2)) =
                (api.work_create, api.work_release, api.report_actual2)
            {
                let work = work_create();
                if !work.is_null() {
                    if let Some(set_start) = api.work_set_start {
                        set_start(work, start_ns);
                    }
                    if let Some(set_total) = api.work_set_total {
                        set_total(work, report_total_ns);
                    }
                    if let Some(set_cpu) = api.work_set_cpu {
                        set_cpu(work, actual_duration_ns);
                    }
                    if let Some(set_gpu) = api.work_set_gpu {
                        let gpu_dur = (target_ns / 2).max(1);
                        set_gpu(work, gpu_dur);
                    }
                    let _ = report2(session, work);
                    work_release(work);
                    return;
                }
            }

            (api.report_actual)(session, report_total_ns);
        }
    }
}

pub fn adpf_destroy() {
    let session = ADPF_SESSION.swap(std::ptr::null_mut(), Ordering::SeqCst);
    if !session.is_null() {
        let guard = API.lock().unwrap();
        if let Some(ref api) = *guard {
            unsafe {
                (api.close_session)(session);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// C-ABI Exports (matching adpf_manager.h)
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_adpf_init(target_fps: c_int) {
    adpf_init(target_fps);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_adpf_begin_frame() {
    adpf_begin_frame();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_adpf_end_frame() {
    adpf_end_frame();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_adpf_destroy() {
    adpf_destroy();
}
