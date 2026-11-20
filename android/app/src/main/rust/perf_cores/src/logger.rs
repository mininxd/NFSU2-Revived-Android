//! Fast in-memory circular ring-buffer logger for GameLog and NetLog in pure Rust.
//! Eliminates Java GC stutter and synchronized object allocation during gameplay.

use std::collections::VecDeque;
use std::sync::Mutex;
use std::time::SystemTime;

use jni::objects::{JClass, JString};
use jni::sys::{jbyteArray, jint};
use jni::JNIEnv;

const MAX_RING_ENTRIES: usize = 1024;

#[derive(Clone)]
struct LogRecord {
    epoch_ms: u64,
    cat: i32, // 0 = Game, 1 = Net
    level: String,
    tag: String,
    msg: String,
}

struct RingLogger {
    records: VecDeque<LogRecord>,
}

impl RingLogger {
    fn new() -> Self {
        Self {
            records: VecDeque::with_capacity(MAX_RING_ENTRIES),
        }
    }

    fn push(&mut self, cat: i32, level: &str, tag: &str, msg: &str) {
        if self.records.len() >= MAX_RING_ENTRIES {
            self.records.pop_front();
        }
        let epoch_ms = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64;

        self.records.push_back(LogRecord {
            epoch_ms,
            cat,
            level: level.to_string(),
            tag: tag.to_string(),
            msg: msg.to_string(),
        });
    }

    fn dump_bytes(&self, cat_filter: i32) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.records.len() * 128);
        for r in &self.records {
            if cat_filter != -1 && r.cat != cat_filter {
                continue;
            }
            let secs = r.epoch_ms / 1000;
            let ms = r.epoch_ms % 1000;
            let line = format!("[{:010}.{:03}] [{}/{}]: {}\n", secs, ms, r.level, r.tag, r.msg);
            out.extend_from_slice(line.as_bytes());
        }
        out
    }
}

static LOGGER_STATE: Mutex<Option<RingLogger>> = Mutex::new(None);

fn with_logger<F, R>(f: F) -> R
where
    F: FnOnce(&mut RingLogger) -> R,
{
    let mut lock = LOGGER_STATE.lock().unwrap();
    if lock.is_none() {
        *lock = Some(RingLogger::new());
    }
    f(lock.as_mut().unwrap())
}

// ---------------------------------------------------------------------------
// JNI Exports for io.mininxd.nfsu2.GameLog and io.mininxd.nfsu2.net.NetLog
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GameLog_nativeRingLog(
    mut env: JNIEnv,
    _class: JClass,
    level: JString,
    tag: JString,
    msg: JString,
) {
    let l = env.get_string(&level).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let t = env.get_string(&tag).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let m = env.get_string(&msg).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();

    with_logger(|logger| {
        logger.push(0, &l, &t, &m);
    });
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_GameLog_nativeRingGetBytes(
    env: JNIEnv,
    _class: JClass,
) -> jbyteArray {
    let bytes = with_logger(|logger| logger.dump_bytes(0));
    if let Ok(arr) = env.byte_array_from_slice(&bytes) {
        arr.into_raw()
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_net_NetLog_nativeRingLog(
    mut env: JNIEnv,
    _class: JClass,
    level: JString,
    tag: JString,
    msg: JString,
) {
    let l = env.get_string(&level).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let t = env.get_string(&tag).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let m = env.get_string(&msg).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();

    with_logger(|logger| {
        logger.push(1, &l, &t, &m);
    });
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_net_NetLog_nativeRingGetBytes(
    env: JNIEnv,
    _class: JClass,
) -> jbyteArray {
    let bytes = with_logger(|logger| logger.dump_bytes(1));
    if let Ok(arr) = env.byte_array_from_slice(&bytes) {
        arr.into_raw()
    } else {
        std::ptr::null_mut()
    }
}
