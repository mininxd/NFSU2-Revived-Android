//! Pure Rust synchronization deadlock detector and race load stall guard for NFSU2 Android.
//!
//! When transitioning from free roam or menus into a race, asynchronous asset streaming
//! threads and the main thread contend for global queue locks (such as the FileLoader lock at
//! 0x00864EFC) and per-resource heap locks. On modern multi-core devices or under serialized
//! cooperative guest execution, circular wait conditions (Thread 1 -> CS A -> CS B;
//! Thread 6 -> CS B -> CS A) cause permanent freeze stalls where the loading screen never ends.
//!
//! This module analyzes the thread wait-for graph, detects cycles in O(V) time, and signals
//! an immediate bypass/loan to prevent permanent stalls.

use std::sync::atomic::{AtomicU32, Ordering};

pub const MAX_GRAPH_DEPTH: usize = 8;
pub const SPIN_STALL_THRESHOLD: u32 = 400;

static DEADLOCK_CYCLES_BROKEN: AtomicU32 = AtomicU32::new(0);
static SPIN_STALLS_BROKEN: AtomicU32 = AtomicU32::new(0);

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeadlockStatus {
    Ok = 0,
    DeadlockCycle = 1,
    SpinStall = 2,
}

/// Detects if `current_thread` is involved in a circular wait cycle with `initial_owner`.
///
/// `wait_pairs`: Array of (waiting_thread_id, wanted_cs_owner_id).
pub fn detect_cycle(
    current_thread: u32,
    _cs_addr: u32,
    initial_owner: u32,
    wait_pairs: &[(u32, u32)],
) -> bool {
    if initial_owner == 0 || initial_owner == current_thread {
        return false;
    }

    let mut walker = initial_owner;
    for _ in 0..MAX_GRAPH_DEPTH {
        if walker == current_thread {
            return true;
        }

        // Find what thread `walker` is waiting for
        let mut next = 0;
        for &(tid, owner) in wait_pairs {
            if tid == walker {
                next = owner;
                break;
            }
        }

        if next == 0 || next == walker {
            break;
        }
        walker = next;
    }

    false
}

/// External C ABI for checking critical section contention deadlock.
///
/// Returns:
///  0 = Ok, keep waiting
///  1 = Deadlock cycle detected, grant bypass
///  2 = Spin count exceeded stall threshold, grant bypass
#[no_mangle]
pub unsafe extern "C" fn nfs_rust_detect_critical_deadlock(
    current_thread: u32,
    cs_addr: u32,
    initial_owner: u32,
    spins: u32,
    wait_pairs: *const u32,
    pair_count: usize,
) -> i32 {
    if spins >= SPIN_STALL_THRESHOLD {
        SPIN_STALLS_BROKEN.fetch_add(1, Ordering::Relaxed);
        return DeadlockStatus::SpinStall as i32;
    }

    if wait_pairs.is_null() || pair_count == 0 {
        return DeadlockStatus::Ok as i32;
    }

    let slice = std::slice::from_raw_parts(wait_pairs as *const (u32, u32), pair_count);
    if detect_cycle(current_thread, cs_addr, initial_owner, slice) {
        DEADLOCK_CYCLES_BROKEN.fetch_add(1, Ordering::Relaxed);
        return DeadlockStatus::DeadlockCycle as i32;
    }

    DeadlockStatus::Ok as i32
}

/// Returns the total number of deadlock cycles resolved by Rust sync guard.
#[no_mangle]
pub extern "C" fn nfs_rust_deadlock_cycles_resolved() -> u32 {
    DEADLOCK_CYCLES_BROKEN.load(Ordering::Relaxed)
}

/// Returns the total number of prolonged spin stalls resolved by Rust sync guard.
#[no_mangle]
pub extern "C" fn nfs_rust_spin_stalls_resolved() -> u32 {
    SPIN_STALLS_BROKEN.load(Ordering::Relaxed)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_two_thread_cycle() {
        // Thread 1 owns CS_A, wants CS_B (owned by Thread 6)
        // Thread 6 owns CS_B, wants CS_A (owned by Thread 1)
        // wait_pairs: (waiting_tid, owner_tid)
        let pairs = [(6, 1)]; // Thread 6 is waiting for Thread 1
        assert!(detect_cycle(1, 0x10806348, 6, &pairs));
    }

    #[test]
    fn test_three_thread_cycle() {
        // Thread 1 wants B (owner 2)
        // Thread 2 wants C (owner 3)
        // Thread 3 wants A (owner 1)
        let pairs = [(2, 3), (3, 1)];
        assert!(detect_cycle(1, 0x1000, 2, &pairs));
    }

    #[test]
    fn test_no_cycle() {
        // Thread 1 wants B (owner 2)
        // Thread 2 is running and not waiting on anything
        let pairs: [(u32, u32); 0] = [];
        assert!(!detect_cycle(1, 0x1000, 2, &pairs));

        // Thread 1 wants B (owner 2)
        // Thread 2 wants C (owner 3)
        // Thread 3 is not waiting
        let pairs = [(2, 3)];
        assert!(!detect_cycle(1, 0x1000, 2, &pairs));
    }

    #[test]
    fn test_spin_stall_threshold() {
        unsafe {
            assert_eq!(
                nfs_rust_detect_critical_deadlock(1, 0x1000, 2, 450, std::ptr::null(), 0),
                DeadlockStatus::SpinStall as i32
            );
        }
    }
}
