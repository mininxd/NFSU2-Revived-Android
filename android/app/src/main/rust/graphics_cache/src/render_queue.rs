//! Asynchronous D3D9 Render Command Queue.
//!
//! Provides a lock-free circular ring-buffer queue enabling the game logic
//! and D3D9/Vulkan translation to run concurrently on separate CPU cores.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::{Condvar, Mutex};

pub const RENDER_QUEUE_CAPACITY: usize = 16384;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RenderCmd {
    pub op: u16,
    pub method: u16,
    pub args: [u32; 8],
    pub data_ptr: usize,
    pub data_size: u32,
    pub sync_id: u32,
}

impl RenderCmd {
    pub const fn empty() -> Self {
        Self {
            op: 0,
            method: 0,
            args: [0; 8],
            data_ptr: 0,
            data_size: 0,
            sync_id: 0,
        }
    }
}

pub struct RenderCommandQueue {
    buffer: Box<[RenderCmd]>,
    head: AtomicU32,
    tail: AtomicU32,
    enabled: AtomicBool,
    sync_counter: AtomicU32,
    completed_sync: AtomicU32,
    sync_mutex: Mutex<()>,
    sync_cv: Condvar,
    pushed_count: AtomicU64,
    executed_count: AtomicU64,
}

impl RenderCommandQueue {
    pub fn new() -> Self {
        Self {
            buffer: vec![RenderCmd::empty(); RENDER_QUEUE_CAPACITY].into_boxed_slice(),
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
            enabled: AtomicBool::new(false),
            sync_counter: AtomicU32::new(1),
            completed_sync: AtomicU32::new(0),
            sync_mutex: Mutex::new(()),
            sync_cv: Condvar::new(),
            pushed_count: AtomicU64::new(0),
            executed_count: AtomicU64::new(0),
        }
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    pub fn set_enabled(&self, val: bool) {
        self.enabled.store(val, Ordering::Release);
    }

    pub fn push(&self, cmd: RenderCmd) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Acquire);

        if head.wrapping_sub(tail) >= RENDER_QUEUE_CAPACITY as u32 {
            return false;
        }

        let idx = (head as usize) & (RENDER_QUEUE_CAPACITY - 1);
        unsafe {
            let ptr = self.buffer.as_ptr() as *mut RenderCmd;
            ptr.add(idx).write(cmd);
        }

        self.head.store(head.wrapping_add(1), Ordering::Release);
        self.pushed_count.fetch_add(1, Ordering::Relaxed);
        true
    }

    pub fn pop(&self) -> Option<RenderCmd> {
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);

        if tail == head {
            return None;
        }

        let idx = (tail as usize) & (RENDER_QUEUE_CAPACITY - 1);
        let cmd = unsafe {
            let ptr = self.buffer.as_ptr();
            ptr.add(idx).read()
        };

        self.tail.store(tail.wrapping_add(1), Ordering::Release);
        self.executed_count.fetch_add(1, Ordering::Relaxed);

        if cmd.sync_id != 0 {
            self.completed_sync.store(cmd.sync_id, Ordering::Release);
            let _lock = self.sync_mutex.lock().unwrap();
            self.sync_cv.notify_all();
        }

        Some(cmd)
    }

    pub fn drain(&self) {
        let target = self.head.load(Ordering::Relaxed);
        while self.tail.load(Ordering::Acquire) != target {
            std::thread::yield_now();
        }
    }

    pub fn sync(&self) {
        let sync_id = self.sync_counter.fetch_add(1, Ordering::Relaxed);
        let mut cmd = RenderCmd::empty();
        cmd.op = 0xffff; // Sync marker
        cmd.sync_id = sync_id;

        while !self.push(cmd) {
            std::thread::yield_now();
        }

        let mut lock = self.sync_mutex.lock().unwrap();
        while self.completed_sync.load(Ordering::Acquire) < sync_id {
            lock = self.sync_cv.wait(lock).unwrap();
        }
    }

    pub fn stats(&self, out_pushed: &mut u64, out_executed: &mut u64) {
        *out_pushed = self.pushed_count.load(Ordering::Relaxed);
        *out_executed = self.executed_count.load(Ordering::Relaxed);
    }
}

static mut GLOBAL_QUEUE: Option<RenderCommandQueue> = None;

pub fn get_queue() -> &'static RenderCommandQueue {
    unsafe {
        if GLOBAL_QUEUE.is_none() {
            GLOBAL_QUEUE = Some(RenderCommandQueue::new());
        }
        GLOBAL_QUEUE.as_ref().unwrap()
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_set_enabled(enabled: i32) {
    get_queue().set_enabled(enabled != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_is_enabled() -> i32 {
    if get_queue().is_enabled() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_push(cmd: *const RenderCmd) -> i32 {
    if cmd.is_null() {
        return 0;
    }
    if get_queue().push(*cmd) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_pop(out_cmd: *mut RenderCmd) -> i32 {
    if out_cmd.is_null() {
        return 0;
    }
    match get_queue().pop() {
        Some(cmd) => {
            *out_cmd = cmd;
            1
        }
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_drain() {
    get_queue().drain();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_sync() {
    get_queue().sync();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_render_queue_stats(out_pushed: *mut u64, out_executed: *mut u64) {
    let mut pushed = 0u64;
    let mut executed = 0u64;
    get_queue().stats(&mut pushed, &mut executed);
    if !out_pushed.is_null() {
        *out_pushed = pushed;
    }
    if !out_executed.is_null() {
        *out_executed = executed;
    }
}
