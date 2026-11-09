//! High-Performance Guest Memory Heap Manager in Rust for NFSU2 Android.
//!
//! Replaces linear C++ std::map traversal and fragmented red-black tree host allocations
//! with cache-friendly BTreeMap interval coalescing and fast block accounting.

use std::collections::BTreeMap;
use std::sync::Mutex;

pub const PROCESS_HEAP_ID: u32 = 0xF0000100;

#[derive(Clone, Copy, Debug)]
pub struct Block {
    pub capacity: u32,
    pub requested: u32,
}

#[derive(Debug)]
pub struct HeapInstance {
    pub options: u32,
    pub maximum: u32,
    pub live: u64,
    pub blocks: BTreeMap<u32, Block>,
}

impl HeapInstance {
    pub fn new(options: u32, maximum: u32) -> Self {
        Self {
            options,
            maximum,
            live: 0,
            blocks: BTreeMap::new(),
        }
    }
}

pub struct GuestHeapManager {
    free_intervals: BTreeMap<u32, u32>, // address -> capacity
    heaps: BTreeMap<u32, HeapInstance>,
    next_heap_id: u32,
}

impl GuestHeapManager {
    pub fn new(first_arena: u32, first_arena_size: u32, second_arena: u32, second_arena_size: u32) -> Self {
        let mut free = BTreeMap::new();
        if first_arena_size > 0 {
            free.insert(first_arena, first_arena_size);
        }
        if second_arena_size > 0 {
            free.insert(second_arena, second_arena_size);
        }

        let mut heaps = BTreeMap::new();
        heaps.insert(PROCESS_HEAP_ID, HeapInstance::new(0, 0));

        Self {
            free_intervals: free,
            heaps,
            next_heap_id: 0xF0000104,
        }
    }

    pub fn release_range(&mut self, mut address: u32, mut capacity: u32) {
        // Coalesce with preceding adjacent free block
        let prev_key = self.free_intervals.range(..address).next_back().map(|(&k, &v)| (k, v));
        if let Some((before_addr, before_cap)) = prev_key {
            if (before_addr as u64) + (before_cap as u64) == address as u64 {
                address = before_addr;
                capacity += before_cap;
                self.free_intervals.remove(&before_addr);
            }
        }

        // Coalesce with following adjacent free block
        let next_key = self.free_intervals.range(address..).next().map(|(&k, &v)| (k, v));
        if let Some((next_addr, next_cap)) = next_key {
            if (address as u64) + (capacity as u64) == next_addr as u64 {
                capacity += next_cap;
                self.free_intervals.remove(&next_addr);
            }
        }

        self.free_intervals.insert(address, capacity);
    }

    pub fn create_heap(&mut self, options: u32, initial: u32, maximum: u32) -> u32 {
        if maximum > 0 && initial > maximum {
            return 0;
        }
        let id = self.next_heap_id;
        self.next_heap_id = self.next_heap_id.wrapping_add(4);
        self.heaps.insert(id, HeapInstance::new(options, maximum));
        id
    }

    pub fn destroy_heap(&mut self, heap_id: u32) -> bool {
        if heap_id == PROCESS_HEAP_ID {
            return false;
        }
        if let Some(heap) = self.heaps.remove(&heap_id) {
            for (addr, block) in heap.blocks {
                self.release_range(addr, block.capacity);
            }
            true
        } else {
            false
        }
    }

    pub fn alloc(
        &mut self,
        heap_id: u32,
        flags: u32,
        bytes: u32,
        out_commit_cap: &mut u32,
        out_commit_slack: &mut u32,
    ) -> Result<u32, bool> {
        let heap = match self.heaps.get_mut(&heap_id) {
            Some(h) => h,
            None => return Ok(0),
        };

        let req_bytes = bytes.max(1);
        let capacity = ((req_bytes as u64 + 15) & !15) as u64;
        if capacity > u32::MAX as u64 {
            return Ok(0);
        }
        let capacity_u32 = capacity as u32;

        if heap.maximum > 0 && (bytes >= 0x7FFF8 || heap.live + capacity > heap.maximum as u64) {
            return Ok(0);
        }

        // Find lowest address free slot that fits capacity
        let slot_match = self.free_intervals.iter()
            .find(|(_, &size)| size >= capacity_u32)
            .map(|(&addr, &size)| (addr, size));

        let (address, slot_size) = match slot_match {
            Some(s) => s,
            None => {
                let raise_exception = ((flags | heap.options) & 4) != 0;
                return if raise_exception { Err(true) } else { Ok(0) };
            }
        };

        let remaining = slot_size - capacity_u32;
        self.free_intervals.remove(&address);
        if remaining > 0 {
            self.free_intervals.insert(address + capacity_u32, remaining);
        }

        heap.blocks.insert(address, Block {
            capacity: capacity_u32,
            requested: bytes,
        });
        heap.live += capacity;

        *out_commit_cap = capacity_u32;
        *out_commit_slack = remaining.min(4096);

        Ok(address)
    }

    pub fn free(&mut self, heap_id: u32, address: u32) -> bool {
        if address == 0 {
            return true;
        }
        let heap = match self.heaps.get_mut(&heap_id) {
            Some(h) => h,
            None => return false,
        };

        if let Some(block) = heap.blocks.remove(&address) {
            heap.live = heap.live.saturating_sub(block.capacity as u64);
            self.release_range(address, block.capacity);
            true
        } else {
            false
        }
    }

    pub fn size(&self, heap_id: u32, address: u32) -> u32 {
        let heap = match self.heaps.get(&heap_id) {
            Some(h) => h,
            None => return u32::MAX,
        };
        heap.blocks.get(&address).map(|b| b.capacity).unwrap_or(u32::MAX)
    }

    pub fn realloc(
        &mut self,
        heap_id: u32,
        flags: u32,
        address: u32,
        bytes: u32,
        out_commit_cap: &mut u32,
        out_commit_slack: &mut u32,
        out_old_requested: &mut u32,
        out_is_in_place: &mut i32,
    ) -> Result<u32, bool> {
        let heap = match self.heaps.get_mut(&heap_id) {
            Some(h) => h,
            None => return Ok(0),
        };

        let old_block = match heap.blocks.get(&address) {
            Some(&b) => b,
            None => return Ok(0),
        };

        if heap.maximum > 0 && bytes >= 0x7FFF8 {
            return Ok(0);
        }

        *out_old_requested = old_block.requested;

        // Shrinking or fitting inside existing capacity
        if bytes <= old_block.capacity {
            if let Some(b) = heap.blocks.get_mut(&address) {
                b.requested = bytes;
            }
            *out_is_in_place = 1;
            *out_commit_cap = old_block.capacity;
            *out_commit_slack = 0;
            return Ok(address);
        }

        let new_capacity = ((bytes as u64 + 15) & !15) as u64;
        if new_capacity > u32::MAX as u64 {
            return Ok(0);
        }
        let new_capacity_u32 = new_capacity as u32;
        let delta = new_capacity_u32 - old_block.capacity;

        // Check if adjacent free range can expand in place
        let adjacent_addr = address + old_block.capacity;
        let adjacent_match = self.free_intervals.get(&adjacent_addr).copied();

        if let Some(adj_size) = adjacent_match {
            if adj_size >= delta && (heap.maximum == 0 || heap.live + (delta as u64) <= heap.maximum as u64) {
                let remaining = adj_size - delta;
                self.free_intervals.remove(&adjacent_addr);
                if remaining > 0 {
                    self.free_intervals.insert(address + new_capacity_u32, remaining);
                }

                if let Some(b) = heap.blocks.get_mut(&address) {
                    b.capacity = new_capacity_u32;
                    b.requested = bytes;
                }
                heap.live += delta as u64;

                *out_is_in_place = 1;
                *out_commit_cap = new_capacity_u32;
                *out_commit_slack = remaining.min(4096);
                return Ok(address);
            }
        }

        // Cannot grow in place
        if (flags & 16) != 0 {
            // HEAP_REALLOC_IN_PLACE_ONLY
            return Ok(0);
        }

        // Must move to new block
        *out_is_in_place = 0;
        self.alloc(heap_id, flags, bytes, out_commit_cap, out_commit_slack)
    }
}

static GUEST_HEAP: Mutex<Option<GuestHeapManager>> = Mutex::new(None);

// ---------------------------------------------------------------------------
// C ABI Interface
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_init(
    first_arena: u32,
    first_arena_size: u32,
    second_arena: u32,
    second_arena_size: u32,
) -> i32 {
    let mut guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return -1,
    };
    *guard = Some(GuestHeapManager::new(
        first_arena,
        first_arena_size,
        second_arena,
        second_arena_size,
    ));
    0
}

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_create(options: u32, initial: u32, maximum: u32) -> u32 {
    let mut guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return 0,
    };
    if let Some(mgr) = guard.as_mut() {
        mgr.create_heap(options, initial, maximum)
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_destroy(heap_id: u32) -> i32 {
    let mut guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return 0,
    };
    if let Some(mgr) = guard.as_mut() {
        if mgr.destroy_heap(heap_id) { 1 } else { 0 }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_alloc(
    heap_id: u32,
    flags: u32,
    bytes: u32,
    out_commit_cap: *mut u32,
    out_commit_slack: *mut u32,
    out_exception: *mut i32,
) -> u32 {
    if out_commit_cap.is_null() || out_commit_slack.is_null() || out_exception.is_null() {
        return 0;
    }
    *out_exception = 0;

    let mut guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return 0,
    };

    if let Some(mgr) = guard.as_mut() {
        let mut cap = 0u32;
        let mut slack = 0u32;
        match mgr.alloc(heap_id, flags, bytes, &mut cap, &mut slack) {
            Ok(addr) => {
                *out_commit_cap = cap;
                *out_commit_slack = slack;
                addr
            }
            Err(_exc) => {
                *out_exception = 1;
                0
            }
        }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_free(heap_id: u32, address: u32) -> i32 {
    let mut guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return 0,
    };
    if let Some(mgr) = guard.as_mut() {
        if mgr.free(heap_id, address) { 1 } else { 0 }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_size(heap_id: u32, address: u32) -> u32 {
    let guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return u32::MAX,
    };
    if let Some(mgr) = guard.as_ref() {
        mgr.size(heap_id, address)
    } else {
        u32::MAX
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_guest_heap_realloc(
    heap_id: u32,
    flags: u32,
    address: u32,
    bytes: u32,
    out_commit_cap: *mut u32,
    out_commit_slack: *mut u32,
    out_old_requested: *mut u32,
    out_is_in_place: *mut i32,
) -> u32 {
    if out_commit_cap.is_null() || out_commit_slack.is_null()
        || out_old_requested.is_null() || out_is_in_place.is_null()
    {
        return 0;
    }

    let mut guard = match GUEST_HEAP.lock() {
        Ok(g) => g,
        Err(_) => return 0,
    };

    if let Some(mgr) = guard.as_mut() {
        let mut cap = 0u32;
        let mut slack = 0u32;
        let mut old_req = 0u32;
        let mut in_place = 0i32;

        match mgr.realloc(
            heap_id,
            flags,
            address,
            bytes,
            &mut cap,
            &mut slack,
            &mut old_req,
            &mut in_place,
        ) {
            Ok(new_addr) => {
                *out_commit_cap = cap;
                *out_commit_slack = slack;
                *out_old_requested = old_req;
                *out_is_in_place = in_place;
                new_addr
            }
            Err(_) => 0,
        }
    } else {
        0
    }
}
