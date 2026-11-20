//! Win32 Guest Thread, Mutex, Event and Timer Subsystems in Safe Rust for NFSU2 Android.
//!
//! Provides deterministic handle allocation, named object resolution, and generation-tracked
//! event signaling matching Win32 kernel semantics.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex, RwLock};

pub const HANDLE_THREAD_BASE: u32 = 0xF0030000;
pub const HANDLE_EVENT_BASE: u32 = 0xF0040000;
pub const HANDLE_MUTEX_BASE: u32 = 0xF0080000;

pub const ERROR_ALREADY_EXISTS: u32 = 183;
pub const ERROR_INVALID_HANDLE: u32 = 6;
pub const ERROR_NOT_OWNER: u32 = 288;

// ---------------------------------------------------------------------------
// Guest Mutex
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct GuestMutex {
    pub owner: u32,
    pub depth: u32,
    pub abandoned: bool,
}

impl GuestMutex {
    pub fn new(initial_owner: u32) -> Self {
        Self {
            owner: initial_owner,
            depth: if initial_owner != 0 { 1 } else { 0 },
            abandoned: false,
        }
    }
}

// ---------------------------------------------------------------------------
// Guest Event
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct GuestEvent {
    pub manual_reset: bool,
    pub signaled: bool,
    pub generation: u32,
}

impl GuestEvent {
    pub fn new(manual_reset: bool, initially_signaled: bool) -> Self {
        Self {
            manual_reset,
            signaled: initially_signaled,
            generation: 0,
        }
    }

    pub fn set(&mut self) {
        self.signaled = true;
        self.generation = self.generation.wrapping_add(1);
    }

    pub fn reset(&mut self) {
        self.signaled = false;
    }
}

// ---------------------------------------------------------------------------
// Guest Thread
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct GuestThread {
    pub id: u32,
    pub handle: u32,
    pub start_address: u32,
    pub parameter: u32,
    pub stack_base: u32,
    pub tib: u32,
    pub exit_code: u32,
    pub suspended: bool,
    pub done: bool,
    pub priority: i32,
}

impl GuestThread {
    pub fn new(id: u32, handle: u32, start: u32, param: u32, stack: u32, tib: u32) -> Self {
        Self {
            id,
            handle,
            start_address: start,
            parameter: param,
            stack_base: stack,
            tib,
            exit_code: 259, // STILL_ACTIVE
            suspended: false,
            done: false,
            priority: 0,
        }
    }
}

// ---------------------------------------------------------------------------
// Synchronization Registry
// ---------------------------------------------------------------------------

pub struct SyncRegistry {
    mutex_handles: HashMap<u32, Arc<Mutex<GuestMutex>>>,
    named_mutexes: HashMap<String, Arc<Mutex<GuestMutex>>>,
    next_mutex_handle: u32,

    event_handles: HashMap<u32, Arc<Mutex<GuestEvent>>>,
    named_events: HashMap<String, Arc<Mutex<GuestEvent>>>,
    next_event_handle: u32,

    thread_handles: HashMap<u32, Arc<Mutex<GuestThread>>>,
    next_thread_id: u32,
    next_thread_handle: u32,
}

impl SyncRegistry {
    pub fn new() -> Self {
        Self {
            mutex_handles: HashMap::new(),
            named_mutexes: HashMap::new(),
            next_mutex_handle: HANDLE_MUTEX_BASE,

            event_handles: HashMap::new(),
            named_events: HashMap::new(),
            next_event_handle: HANDLE_EVENT_BASE,

            thread_handles: HashMap::new(),
            next_thread_id: 2,
            next_thread_handle: HANDLE_THREAD_BASE,
        }
    }

    pub fn create_mutex(&mut self, initial_owner: u32, name: Option<&str>) -> (u32, u32) {
        let mut error = 0;
        let mutex_arc = if let Some(n) = name {
            if let Some(existing) = self.named_mutexes.get(n) {
                error = ERROR_ALREADY_EXISTS;
                existing.clone()
            } else {
                let m = Arc::new(Mutex::new(GuestMutex::new(initial_owner)));
                self.named_mutexes.insert(n.to_string(), m.clone());
                m
            }
        } else {
            Arc::new(Mutex::new(GuestMutex::new(initial_owner)))
        };

        let handle = self.next_mutex_handle;
        self.next_mutex_handle = self.next_mutex_handle.wrapping_add(1);
        self.mutex_handles.insert(handle, mutex_arc);
        (handle, error)
    }

    pub fn release_mutex(&mut self, handle: u32, caller_thread: u32) -> Result<(), u32> {
        let mutex_arc = self.mutex_handles.get(&handle).ok_or(ERROR_INVALID_HANDLE)?;
        let mut m = mutex_arc.lock().unwrap();
        if m.owner != caller_thread {
            return Err(ERROR_NOT_OWNER);
        }
        if m.depth > 0 {
            m.depth -= 1;
            if m.depth == 0 {
                m.owner = 0;
            }
        }
        Ok(())
    }

    pub fn create_event(&mut self, manual_reset: bool, initial_state: bool, name: Option<&str>) -> (u32, u32) {
        let mut error = 0;
        let event_arc = if let Some(n) = name {
            if let Some(existing) = self.named_events.get(n) {
                error = ERROR_ALREADY_EXISTS;
                existing.clone()
            } else {
                let ev = Arc::new(Mutex::new(GuestEvent::new(manual_reset, initial_state)));
                self.named_events.insert(n.to_string(), ev.clone());
                ev
            }
        } else {
            Arc::new(Mutex::new(GuestEvent::new(manual_reset, initial_state)))
        };

        let handle = self.next_event_handle;
        self.next_event_handle = self.next_event_handle.wrapping_add(1);
        self.event_handles.insert(handle, event_arc);
        (handle, error)
    }

    pub fn set_event(&mut self, handle: u32) -> Result<(), u32> {
        let event_arc = self.event_handles.get(&handle).ok_or(ERROR_INVALID_HANDLE)?;
        let mut ev = event_arc.lock().unwrap();
        ev.set();
        Ok(())
    }

    pub fn reset_event(&mut self, handle: u32) -> Result<(), u32> {
        let event_arc = self.event_handles.get(&handle).ok_or(ERROR_INVALID_HANDLE)?;
        let mut ev = event_arc.lock().unwrap();
        ev.reset();
        Ok(())
    }

    pub fn register_thread(&mut self, start: u32, param: u32, stack: u32, tib: u32) -> (u32, u32) {
        let id = self.next_thread_id;
        self.next_thread_id += 1;
        let handle = self.next_thread_handle;
        self.next_thread_handle += 1;

        let thread = Arc::new(Mutex::new(GuestThread::new(id, handle, start, param, stack, tib)));
        self.thread_handles.insert(handle, thread);
        (id, handle)
    }
}

static SYNC_REGISTRY: RwLock<Option<SyncRegistry>> = RwLock::new(None);

fn with_sync_registry<F, R>(f: F) -> R
where
    F: FnOnce(&mut SyncRegistry) -> R,
{
    let mut guard = SYNC_REGISTRY.write().unwrap();
    if guard.is_none() {
        *guard = Some(SyncRegistry::new());
    }
    f(guard.as_mut().unwrap())
}

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_create_mutex(initial_owner: u32) -> u32 {
    let (handle, _) = with_sync_registry(|reg| reg.create_mutex(initial_owner, None));
    handle
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_release_mutex(handle: u32, caller_thread: u32) -> u32 {
    let res = with_sync_registry(|reg| reg.release_mutex(handle, caller_thread));
    if res.is_ok() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_create_event(manual_reset: u32, initial_state: u32) -> u32 {
    let (handle, _) = with_sync_registry(|reg| {
        reg.create_event(manual_reset != 0, initial_state != 0, None)
    });
    handle
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_set_event(handle: u32) -> u32 {
    let res = with_sync_registry(|reg| reg.set_event(handle));
    if res.is_ok() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_reset_event(handle: u32) -> u32 {
    let res = with_sync_registry(|reg| reg.reset_event(handle));
    if res.is_ok() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_guest_register_thread(start: u32, param: u32, stack: u32, tib: u32, out_handle: *mut u32) -> u32 {
    let (id, handle) = with_sync_registry(|reg| reg.register_thread(start, param, stack, tib));
    if !out_handle.is_null() {
        *out_handle = handle;
    }
    id
}
