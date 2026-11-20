//! Win32 Guest Kernel Emulation Subsystem in Safe Rust for NFSU2 Android.
//!
//! Provides thread-local storage (TLS) slot allocation, critical section synchronization
//! with automatic deadlock cycle detection, guest string conversions, and error state tracking.

use std::collections::{BTreeMap, HashMap};
use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Mutex, RwLock};

pub const TLS_MAX_SLOTS: usize = 1088;
pub const ERROR_SUCCESS: u32 = 0;
pub const ERROR_INVALID_PARAMETER: u32 = 87;
pub const ERROR_NO_MORE_ITEMS: u32 = 259;
pub const ERROR_INVALID_HANDLE: u32 = 6;

// ---------------------------------------------------------------------------
// Thread-Local Storage (TLS) Manager
// ---------------------------------------------------------------------------

pub struct TlsManager {
    used_slots: [bool; TLS_MAX_SLOTS],
}

impl TlsManager {
    pub const fn new() -> Self {
        Self {
            used_slots: [false; TLS_MAX_SLOTS],
        }
    }

    pub fn alloc(&mut self) -> Result<u32, u32> {
        for (index, used) in self.used_slots.iter_mut().enumerate() {
            if !*used {
                *used = true;
                return Ok(index as u32);
            }
        }
        Err(ERROR_NO_MORE_ITEMS)
    }

    pub fn free(&mut self, slot: u32) -> Result<(), u32> {
        let idx = slot as usize;
        if idx < TLS_MAX_SLOTS && self.used_slots[idx] {
            self.used_slots[idx] = false;
            Ok(())
        } else {
            Err(ERROR_INVALID_PARAMETER)
        }
    }

    pub fn is_valid(&self, slot: u32) -> bool {
        let idx = slot as usize;
        idx < TLS_MAX_SLOTS && self.used_slots[idx]
    }
}

static TLS_GLOBAL: Mutex<TlsManager> = Mutex::new(TlsManager::new());
thread_local! {
    static TLS_THREAD_VALUES: std::cell::RefCell<[u32; TLS_MAX_SLOTS]> = const { std::cell::RefCell::new([0u32; TLS_MAX_SLOTS]) };
    static LAST_ERROR: AtomicU32 = const { AtomicU32::new(0) };
}

// ---------------------------------------------------------------------------
// Guest Critical Section Synchronization & Deadlock Cycle Breaker
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct GuestCriticalSection {
    pub address: u32,
    pub spin_count: u32,
    pub owner_thread: u32,
    pub recursion_count: u32,
    pub lock: Mutex<()>,
}

impl GuestCriticalSection {
    pub fn new(address: u32, spin: u32) -> Self {
        Self {
            address,
            spin_count: spin,
            owner_thread: 0,
            recursion_count: 0,
            lock: Mutex::new(()),
        }
    }
}

pub struct CriticalSectionRegistry {
    sections: HashMap<u32, std::sync::Arc<GuestCriticalSection>>,
    thread_waiting: HashMap<u32, u32>, // thread_id -> critical_section_address
    bypassed_threads: HashMap<u32, Vec<u32>>,
}

impl CriticalSectionRegistry {
    pub fn new() -> Self {
        Self {
            sections: HashMap::new(),
            thread_waiting: HashMap::new(),
            bypassed_threads: HashMap::new(),
        }
    }

    pub fn initialize(&mut self, address: u32, spin: u32) {
        self.sections.insert(
            address,
            std::sync::Arc::new(GuestCriticalSection::new(address, spin)),
        );
    }

    pub fn get_or_create(&mut self, address: u32) -> std::sync::Arc<GuestCriticalSection> {
        self.sections
            .entry(address)
            .or_insert_with(|| std::sync::Arc::new(GuestCriticalSection::new(address, 0)))
            .clone()
    }

    pub fn delete(&mut self, address: u32) {
        self.sections.remove(&address);
        self.thread_waiting.retain(|_, cs_addr| *cs_addr != address);
    }

    pub fn set_waiting(&mut self, thread_id: u32, address: u32) {
        self.thread_waiting.insert(thread_id, address);
    }

    pub fn clear_waiting(&mut self, thread_id: u32) {
        self.thread_waiting.remove(&thread_id);
    }

    pub fn is_deadlocked(&self, owner: u32, target_thread: u32) -> bool {
        if owner == 0 || owner == target_thread {
            return false;
        }
        let mut walker = owner;
        for _ in 0..8 {
            if walker == target_thread {
                return true;
            }
            if let Some(&wanted_cs_addr) = self.thread_waiting.get(&walker) {
                if let Some(cs) = self.sections.get(&wanted_cs_addr) {
                    let next_owner = cs.owner_thread;
                    if next_owner == 0 || next_owner == walker {
                        break;
                    }
                    walker = next_owner;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        false
    }

    pub fn record_bypass(&mut self, thread_id: u32, address: u32) {
        self.bypassed_threads
            .entry(thread_id)
            .or_default()
            .push(address);
    }
}

static CS_REGISTRY: RwLock<Option<CriticalSectionRegistry>> = RwLock::new(None);

fn with_cs_registry<F, R>(f: F) -> R
where
    F: FnOnce(&mut CriticalSectionRegistry) -> R,
{
    let mut guard = CS_REGISTRY.write().unwrap();
    if guard.is_none() {
        *guard = Some(CriticalSectionRegistry::new());
    }
    f(guard.as_mut().unwrap())
}

// ---------------------------------------------------------------------------
// Guest String Operations
// ---------------------------------------------------------------------------

pub fn decode_ansi_string(ptr: *const u8, max_len: usize) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    let mut len = 0;
    unsafe {
        while len < max_len && *ptr.add(len) != 0 {
            len += 1;
        }
        let slice = std::slice::from_raw_parts(ptr, len);
        Some(String::from_utf8_lossy(slice).into_owned())
    }
}

pub fn decode_utf16_string(ptr: *const u16, max_chars: usize) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    let mut len = 0;
    unsafe {
        while len < max_chars && *ptr.add(len) != 0 {
            len += 1;
        }
        let slice = std::slice::from_raw_parts(ptr, len);
        String::from_utf16(slice).ok()
    }
}

// ---------------------------------------------------------------------------
// C-ABI Exports for Win32 Emulation
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_get_last_error() -> u32 {
    LAST_ERROR.with(|e| e.load(Ordering::Acquire))
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_set_last_error(error: u32) {
    LAST_ERROR.with(|e| e.store(error, Ordering::Release));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_tls_alloc() -> u32 {
    let mut mgr = TLS_GLOBAL.lock().unwrap();
    match mgr.alloc() {
        Ok(slot) => {
            TLS_THREAD_VALUES.with(|vals| {
                vals.borrow_mut()[slot as usize] = 0;
            });
            slot
        }
        Err(code) => {
            nfs_guest_set_last_error(code);
            u32::MAX
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_tls_free(slot: u32) -> u32 {
    let mut mgr = TLS_GLOBAL.lock().unwrap();
    match mgr.free(slot) {
        Ok(()) => {
            TLS_THREAD_VALUES.with(|vals| {
                if (slot as usize) < TLS_MAX_SLOTS {
                    vals.borrow_mut()[slot as usize] = 0;
                }
            });
            1
        }
        Err(code) => {
            nfs_guest_set_last_error(code);
            0
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_tls_get_value(slot: u32) -> u32 {
    let mgr = TLS_GLOBAL.lock().unwrap();
    if mgr.is_valid(slot) {
        nfs_guest_set_last_error(ERROR_SUCCESS);
        TLS_THREAD_VALUES.with(|vals| vals.borrow()[slot as usize])
    } else {
        nfs_guest_set_last_error(ERROR_INVALID_PARAMETER);
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_tls_set_value(slot: u32, value: u32) -> u32 {
    let mgr = TLS_GLOBAL.lock().unwrap();
    if mgr.is_valid(slot) {
        TLS_THREAD_VALUES.with(|vals| vals.borrow_mut()[slot as usize] = value);
        1
    } else {
        nfs_guest_set_last_error(ERROR_INVALID_PARAMETER);
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_critical_init(address: u32, spin: u32) {
    with_cs_registry(|reg| reg.initialize(address, spin));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_critical_delete(address: u32) {
    with_cs_registry(|reg| reg.delete(address));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_critical_check_deadlock(owner: u32, current_thread: u32) -> u32 {
    let is_deadlocked = with_cs_registry(|reg| reg.is_deadlocked(owner, current_thread));
    if is_deadlocked { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_critical_set_waiting(thread_id: u32, address: u32) {
    with_cs_registry(|reg| reg.set_waiting(thread_id, address));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_critical_clear_waiting(thread_id: u32) {
    with_cs_registry(|reg| reg.clear_waiting(thread_id));
}
