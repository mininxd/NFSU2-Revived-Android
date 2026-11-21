//! Asynchronous Vulkan Pipeline Cache Manager.
//!
//! Handles safe loading, validation, crash detection, and asynchronous atomic
//! persistence of compiled Vulkan pipeline caches.

use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

pub const VK_PIPELINE_CACHE_HEADER_VERSION_ONE: u32 = 1;
pub const VK_UUID_SIZE: usize = 16;
pub const MIN_HEADER_SIZE: usize = 32;

/// Validates whether a Vulkan pipeline cache binary blob matches the current physical device.
pub fn matches_device(
    data: &[u8],
    vendor_id: u32,
    device_id: u32,
    uuid: &[u8; VK_UUID_SIZE],
) -> bool {
    if data.len() < MIN_HEADER_SIZE {
        return false;
    }

    let mut header = [0u32; 4];
    for i in 0..4 {
        let offset = i * 4;
        header[i] = u32::from_ne_bytes([
            data[offset],
            data[offset + 1],
            data[offset + 2],
            data[offset + 3],
        ]);
    }

    let length = header[0];
    let version = header[1];
    let file_vendor_id = header[2];
    let file_device_id = header[3];

    if (length as usize) < MIN_HEADER_SIZE || version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE {
        return false;
    }

    if file_vendor_id != vendor_id || file_device_id != device_id {
        return false;
    }

    let file_uuid = &data[16..32];
    file_uuid == uuid
}

/// Asynchronous pipeline cache worker for background persistence.
pub struct PipelineCacheManager {
    cache_path: PathBuf,
    marker_path: PathBuf,
    saved_size: AtomicUsize,
    stop_signal: Arc<AtomicBool>,
    save_trigger: Arc<(Mutex<Option<Vec<u8>>>, Condvar)>,
    worker_handle: Option<JoinHandle<()>>,
}

impl PipelineCacheManager {
    pub fn new<P: AsRef<Path>>(path: P) -> Self {
        let cache_path = path.as_ref().to_path_buf();
        let marker_path = cache_path.with_extension("bin.loading");
        let stop_signal = Arc::new(AtomicBool::new(false));
        let save_trigger = Arc::new((Mutex::new(None), Condvar::new()));

        let stop_clone = Arc::clone(&stop_signal);
        let trigger_clone = Arc::clone(&save_trigger);
        let path_clone = cache_path.clone();

        // Spawn background writer thread
        let worker_handle = thread::Builder::new()
            .name("GfxCacheWriter".into())
            .spawn(move || {
                Self::worker_loop(path_clone, stop_clone, trigger_clone);
            })
            .ok();

        Self {
            cache_path,
            marker_path,
            saved_size: AtomicUsize::new(0),
            stop_signal,
            save_trigger,
            worker_handle,
        }
    }

    /// Safely loads cache data from disk if valid and safe from crashes.
    pub fn load_safe(
        &self,
        vendor_id: u32,
        device_id: u32,
        uuid: &[u8; VK_UUID_SIZE],
    ) -> Option<Vec<u8>> {
        // If crash marker exists, previous driver load crashed; discard stale cache
        if self.marker_path.exists() {
            let _ = fs::remove_file(&self.cache_path);
            let _ = fs::remove_file(&self.marker_path);
            return None;
        }

        if !self.cache_path.is_file() {
            return None;
        }

        let mut file = match File::open(&self.cache_path) {
            Ok(f) => f,
            Err(_) => return None,
        };

        let mut data = Vec::new();
        if file.read_to_end(&mut data).is_err() || data.is_empty() {
            return None;
        }

        if !matches_device(&data, vendor_id, device_id, uuid) {
            return None;
        }

        // Set crash marker before handing to Vulkan driver
        let _ = fs::write(&self.marker_path, b"1");
        self.saved_size.store(data.len(), Ordering::Release);

        Some(data)
    }

    /// Clear the crash marker once driver has successfully ingested cache.
    pub fn mark_load_finished(&self) {
        let _ = fs::remove_file(&self.marker_path);
    }

    /// Queue a snapshot of the driver's cache data for asynchronous background saving.
    pub fn queue_save_async(&self, data: Vec<u8>) {
        if data.is_empty() || data.len() <= self.saved_size.load(Ordering::Acquire) {
            return;
        }

        self.saved_size.store(data.len(), Ordering::Release);
        let (lock, cvar) = &*self.save_trigger;
        if let Ok(mut pending) = lock.lock() {
            *pending = Some(data);
            cvar.notify_one();
        }
    }

    fn worker_loop(
        path: PathBuf,
        stop_signal: Arc<AtomicBool>,
        save_trigger: Arc<(Mutex<Option<Vec<u8>>>, Condvar)>,
    ) {
        let (lock, cvar) = &*save_trigger;

        while !stop_signal.load(Ordering::Acquire) {
            let mut data_to_write = None;

            if let Ok(mut guard) = lock.lock() {
                // Wait for either explicit notification or periodic 15-second flush
                let result = cvar
                    .wait_timeout(guard, Duration::from_secs(15))
                    .unwrap_or_else(|e| e.into_inner());
                guard = result.0;

                if let Some(data) = guard.take() {
                    data_to_write = Some(data);
                }
            }

            if let Some(bytes) = data_to_write {
                Self::atomic_write_to_disk(&path, &bytes);
            }
        }

        // Final flush on stop
        if let Ok(mut guard) = lock.lock() {
            if let Some(bytes) = guard.take() {
                Self::atomic_write_to_disk(&path, &bytes);
            }
        }
    }

    fn atomic_write_to_disk(target_path: &Path, bytes: &[u8]) {
        let tmp_path = target_path.with_extension("bin.tmp");
        if let Ok(mut file) = OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(&tmp_path)
        {
            if file.write_all(bytes).is_ok() && file.flush().is_ok() {
                let _ = fs::rename(&tmp_path, target_path);
            } else {
                let _ = fs::remove_file(&tmp_path);
            }
        }
    }
}

impl Drop for PipelineCacheManager {
    fn drop(&mut self) {
        self.stop_signal.store(true, Ordering::Release);
        let (_lock, cvar) = &*self.save_trigger;
        cvar.notify_all();

        if let Some(handle) = self.worker_handle.take() {
            let _ = handle.join();
        }
        let _ = fs::remove_file(&self.marker_path);
    }
}

/// Atomically backs up and resets the graphics pipeline cache directory.
/// Renames `<cache_dir>/<dir_name>` to `<cache_dir>/<dir_name>-reset-<timestamp>`.
pub fn rotate_and_reset_cache_dir(cache_dir: &str, dir_name: &str) -> Result<String, String> {
    let base = Path::new(cache_dir);
    let target = base.join(dir_name);
    if !target.exists() {
        return Ok(String::new());
    }
    if !target.is_dir() {
        return Err("Cache path exists but is not a directory".into());
    }
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    let backup_name = format!("{}-reset-{}", dir_name, now);
    let backup_path = base.join(&backup_name);

    fs::rename(&target, &backup_path).map_err(|e| format!("Failed to rename cache directory: {}", e))?;
    Ok(backup_name)
}

#[no_mangle]
pub unsafe extern "C" fn Java_io_mininxd_nfsu2_GraphicsCache_nativeResetCache(
    mut env: jni::JNIEnv,
    _class: jni::objects::JClass,
    j_cache_dir: jni::sys::jstring,
    j_dir_name: jni::sys::jstring,
) -> jni::sys::jstring {
    if j_cache_dir.is_null() || j_dir_name.is_null() {
        return std::ptr::null_mut();
    }
    let cache_dir: String = match env.get_string(&jni::objects::JString::from_raw(j_cache_dir)) {
        Ok(s) => s.into(),
        Err(_) => return std::ptr::null_mut(),
    };
    let dir_name: String = match env.get_string(&jni::objects::JString::from_raw(j_dir_name)) {
        Ok(s) => s.into(),
        Err(_) => return std::ptr::null_mut(),
    };
    match rotate_and_reset_cache_dir(&cache_dir, &dir_name) {
        Ok(b) => {
            let res = if b.is_empty() { "OK" } else { &b };
            env.new_string(res).map(|s| s.into_raw()).unwrap_or(std::ptr::null_mut())
        }
        Err(e) => {
            let err_msg = format!("ERROR: {}", e);
            env.new_string(err_msg).map(|s| s.into_raw()).unwrap_or(std::ptr::null_mut())
        }
    }
}
