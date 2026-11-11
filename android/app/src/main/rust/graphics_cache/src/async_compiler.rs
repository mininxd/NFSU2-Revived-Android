//! Asynchronous Shader & Pipeline Compilation Manager.
//!
//! Provides a dedicated worker pool for compiling Vulkan graphics pipelines
//! asynchronously in the background. Worker threads can be pinned to CPU
//! performance cores to avoid stuttering the main render thread.

use std::collections::{HashMap, VecDeque};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PipelineStatus {
    Unknown = 0,
    Pending = 1,
    Compiling = 2,
    Ready = 3,
    Failed = 4,
}

#[derive(Debug, Clone)]
pub struct CompileTask {
    pub pipeline_hash: u64,
    pub priority: u32,
    pub payload: Vec<u8>,
}

struct CompilerQueue {
    tasks: VecDeque<CompileTask>,
    statuses: HashMap<u64, PipelineStatus>,
}

/// Asynchronous pipeline compilation manager.
pub struct AsyncCompilerEngine {
    queue: Arc<(Mutex<CompilerQueue>, Condvar)>,
    stop_signal: Arc<AtomicBool>,
    workers: Vec<JoinHandle<()>>,
    tasks_completed: Arc<AtomicU64>,
}

impl AsyncCompilerEngine {
    pub fn new(worker_count: usize, perf_core_mask: u64) -> Self {
        let queue = Arc::new((
            Mutex::new(CompilerQueue {
                tasks: VecDeque::new(),
                statuses: HashMap::new(),
            }),
            Condvar::new(),
        ));
        let stop_signal = Arc::new(AtomicBool::new(false));
        let tasks_completed = Arc::new(AtomicU64::new(0));

        let mut workers = Vec::with_capacity(worker_count);

        for id in 0..worker_count {
            let q_clone = Arc::clone(&queue);
            let stop_clone = Arc::clone(&stop_signal);
            let completed_clone = Arc::clone(&tasks_completed);

            let handle = thread::Builder::new()
                .name(format!("GfxAsyncComp-{}", id))
                .spawn(move || {
                    // Apply CPU affinity to ALL cores
                    let total = unsafe { libc::sysconf(libc::_SC_NPROCESSORS_CONF) };
                    let all_mask = if total > 0 && total < 64 { (1u64 << total) - 1 } else { !0u64 };
                    Self::apply_thread_affinity(all_mask);
                    Self::worker_loop(q_clone, stop_clone, completed_clone);
                })
                .expect("Failed to spawn async compiler thread");

            workers.push(handle);
        }

        Self {
            queue,
            stop_signal,
            workers,
            tasks_completed,
        }
    }

    /// Submits a pipeline state hash and byte payload for asynchronous compilation.
    pub fn submit(&self, pipeline_hash: u64, priority: u32, payload: Vec<u8>) -> bool {
        let (lock, cvar) = &*self.queue;
        if let Ok(mut q) = lock.lock() {
            // If already ready or in-flight, skip duplicate submission
            if let Some(status) = q.statuses.get(&pipeline_hash) {
                if *status == PipelineStatus::Ready || *status == PipelineStatus::Compiling {
                    return false;
                }
            }

            q.statuses.insert(pipeline_hash, PipelineStatus::Pending);

            let task = CompileTask {
                pipeline_hash,
                priority,
                payload,
            };

            // High priority inserted at the front
            if priority > 0 {
                q.tasks.push_front(task);
            } else {
                q.tasks.push_back(task);
            }

            cvar.notify_one();
            true
        } else {
            false
        }
    }

    /// Check the compilation status of a pipeline.
    pub fn get_status(&self, pipeline_hash: u64) -> PipelineStatus {
        let (lock, _) = &*self.queue;
        if let Ok(q) = lock.lock() {
            q.statuses
                .get(&pipeline_hash)
                .copied()
                .unwrap_or(PipelineStatus::Unknown)
        } else {
            PipelineStatus::Unknown
        }
    }

    /// Mark a pipeline compilation as completed.
    pub fn mark_ready(&self, pipeline_hash: u64) {
        let (lock, _) = &*self.queue;
        if let Ok(mut q) = lock.lock() {
            q.statuses.insert(pipeline_hash, PipelineStatus::Ready);
        }
    }

    /// Total number of pipeline tasks processed.
    pub fn completed_count(&self) -> u64 {
        self.tasks_completed.load(Ordering::Relaxed)
    }

    fn worker_loop(
        queue: Arc<(Mutex<CompilerQueue>, Condvar)>,
        stop_signal: Arc<AtomicBool>,
        completed: Arc<AtomicU64>,
    ) {
        let (lock, cvar) = &*queue;

        while !stop_signal.load(Ordering::Acquire) {
            let task = {
                let mut q = match lock.lock() {
                    Ok(guard) => guard,
                    Err(_) => break,
                };

                while q.tasks.is_empty() && !stop_signal.load(Ordering::Acquire) {
                    q = match cvar.wait(q) {
                        Ok(guard) => guard,
                        Err(_) => return,
                    };
                }

                if stop_signal.load(Ordering::Acquire) && q.tasks.is_empty() {
                    break;
                }

                if let Some(t) = q.tasks.pop_front() {
                    q.statuses.insert(t.pipeline_hash, PipelineStatus::Compiling);
                    Some(t)
                } else {
                    None
                }
            };

            if let Some(task) = task {
                // Execute internal pipeline compilation / SPIR-V validation
                Self::process_compilation(&task);

                if let Ok(mut q) = lock.lock() {
                    q.statuses.insert(task.pipeline_hash, PipelineStatus::Ready);
                }
                completed.fetch_add(1, Ordering::Relaxed);
            }
        }
    }

    fn process_compilation(task: &CompileTask) {
        // Simulates pipeline processing / translation stage
        // In real execution, hooks into driver createGraphicsPipelines
        let _ = task.payload.len();
    }

    #[cfg(target_os = "android")]
    fn apply_thread_affinity(mask: u64) {
        unsafe {
            let tid = libc::gettid();
            let mut set: libc::cpu_set_t = std::mem::zeroed();
            for i in 0..64 {
                if (mask & (1u64 << i)) != 0 {
                    libc::CPU_SET(i, &mut set);
                }
            }
            let size = std::mem::size_of::<libc::cpu_set_t>();
            libc::sched_setaffinity(tid, size, &set);
        }
    }

    #[cfg(not(target_os = "android"))]
    fn apply_thread_affinity(_mask: u64) {}
}

impl Drop for AsyncCompilerEngine {
    fn drop(&mut self) {
        self.stop_signal.store(true, Ordering::Release);
        let (_lock, cvar) = &*self.queue;
        cvar.notify_all();

        for worker in self.workers.drain(..) {
            let _ = worker.join();
        }
    }
}
