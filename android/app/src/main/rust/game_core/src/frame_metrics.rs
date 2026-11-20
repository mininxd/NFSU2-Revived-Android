//! Frame Time Histogram & 1% Low FPS Telemetry Engine for NFSU2 Android.
//!
//! Provides microsecond-precise frame timing, rolling percentiles (1% low, 0.1% low),
//! and frame time jitter variance without Java GC allocation overhead.

use std::sync::Mutex;

const RING_CAPACITY: usize = 512;

pub struct FrameMetricsEngine {
    durations_ns: Mutex<RingBuffer>,
}

struct RingBuffer {
    buffer: [u64; RING_CAPACITY],
    head: usize,
    count: usize,
    last_frame_ns: u64,
}

impl FrameMetricsEngine {
    pub const fn new() -> Self {
        Self {
            durations_ns: Mutex::new(RingBuffer {
                buffer: [16_666_666; RING_CAPACITY], // default ~60fps (16.6ms)
                head: 0,
                count: 0,
                last_frame_ns: 16_666_666,
            }),
        }
    }

    /// Records a newly completed frame's duration in nanoseconds.
    pub fn record_frame(&self, duration_ns: u64) {
        let clamped = duration_ns.clamp(1_000_000, 1_000_000_000); // 1ms to 1000ms
        if let Ok(mut rb) = self.durations_ns.lock() {
            let h = rb.head;
            rb.buffer[h] = clamped;
            rb.head = (h + 1) % RING_CAPACITY;
            if rb.count < RING_CAPACITY {
                rb.count += 1;
            }
            rb.last_frame_ns = clamped;
        }
    }

    /// Computes telemetry metrics into an array:
    /// `[current_fps, avg_fps, p1_low_fps, p01_low_fps, frame_time_ms, jitter_ms]`
    /// Returns true if valid frame metrics were calculated, or false if insufficient frames.
    pub fn compute_metrics(&self, out: &mut [f32; 6]) -> bool {
        let rb = match self.durations_ns.lock() {
            Ok(g) => g,
            Err(_) => return false,
        };

        if rb.count < 2 {
            out[0] = 0.0;
            out[1] = 0.0;
            out[2] = 0.0;
            out[3] = 0.0;
            out[4] = 0.0;
            out[5] = 0.0;
            return false;
        }

        let n = rb.count;
        let last_ns = rb.last_frame_ns;
        let current_fps = if last_ns > 0 { 1_000_000_000.0 / last_ns as f64 } else { 60.0 };

        // Copy active durations into a local scratch array for percentile calculation
        let mut scratch = [0u64; RING_CAPACITY];
        let mut total_ns = 0u64;

        for i in 0..n {
            let val = rb.buffer[i];
            scratch[i] = val;
            total_ns += val;
        }

        let avg_duration_ns = total_ns as f64 / n as f64;
        let avg_fps = if avg_duration_ns > 0.0 { 1_000_000_000.0 / avg_duration_ns } else { 60.0 };

        // Sort scratch array to find 99th and 99.9th percentiles (1% and 0.1% lows)
        scratch[..n].sort_unstable();

        // 99th percentile slowest frame = 1% Low
        let p99_idx = ((n as f64 * 0.99).floor() as usize).min(n - 1);
        let p99_duration_ns = scratch[p99_idx];
        let p1_low_fps = if p99_duration_ns > 0 { 1_000_000_000.0 / p99_duration_ns as f64 } else { avg_fps };

        // 99.9th percentile slowest frame = 0.1% Low
        let p999_idx = ((n as f64 * 0.999).floor() as usize).min(n - 1);
        let p999_duration_ns = scratch[p999_idx];
        let p01_low_fps = if p999_duration_ns > 0 { 1_000_000_000.0 / p999_duration_ns as f64 } else { p1_low_fps };

        // Jitter variance: standard deviation of frame times in milliseconds
        let mut sum_sq_diff = 0.0f64;
        for i in 0..n {
            let diff_ms = (scratch[i] as f64 - avg_duration_ns) / 1_000_000.0;
            sum_sq_diff += diff_ms * diff_ms;
        }
        let jitter_ms = (sum_sq_diff / n as f64).sqrt();

        out[0] = current_fps as f32;
        out[1] = avg_fps as f32;
        out[2] = p1_low_fps as f32;
        out[3] = p01_low_fps as f32;
        out[4] = (last_ns as f32) / 1_000_000.0;
        out[5] = jitter_ms as f32;
        true
    }
}

pub static GLOBAL_FRAME_METRICS: FrameMetricsEngine = FrameMetricsEngine::new();

// ---- C ABI Exports ---------------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_metrics_record_frame(duration_ns: u64) {
    GLOBAL_FRAME_METRICS.record_frame(duration_ns);
}

#[no_mangle]
pub extern "C" fn nfs_metrics_get(out_ptr: *mut f32) {
    if out_ptr.is_null() { return; }
    let mut metrics = [0.0f32; 6];
    GLOBAL_FRAME_METRICS.compute_metrics(&mut metrics);
    unsafe {
        std::ptr::copy_nonoverlapping(metrics.as_ptr(), out_ptr, 6);
    }
}
