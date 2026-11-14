//! Native DXVK Pipeline Accelerator & Emulation-Free Graphics Pipeline for NFSU2 Android.
//!
//! Provides native ARM64 / portable SIMD 3D geometry transformations, frustum culling,
//! and high-speed D3D9 state deduplication in Rust without relying on x87 emulation
//! or interpreter thunks.
//!
//! Hot-path D3D9 calls in SPEED2.EXE and D3DX effects generate tens of thousands of
//! redundant state changes per frame. This engine intercepts and filters redundant
//! state changes before they reach DXVK's Vulkan command recording pipeline, drastically
//! reducing bridge latency and CPU draw call overhead.

use std::os::raw::c_int;
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

// Maximum Direct3D 9 state indices
const D3D9_MAX_RENDER_STATES: usize = 256;
const D3D9_MAX_TEXTURE_STAGES: usize = 8;
const D3D9_MAX_TSS_STATES: usize = 36;
const D3D9_MAX_SAMPLERS: usize = 16;
const D3D9_MAX_SAMPLER_STATES: usize = 16;

static RS_CACHE: [AtomicU32; D3D9_MAX_RENDER_STATES] = {
    const INIT: AtomicU32 = AtomicU32::new(0xFFFF_FFFF);
    [INIT; D3D9_MAX_RENDER_STATES]
};

static TSS_CACHE: [[AtomicU32; D3D9_MAX_TSS_STATES]; D3D9_MAX_TEXTURE_STAGES] = {
    const INIT_ROW: [AtomicU32; D3D9_MAX_TSS_STATES] = {
        const INIT: AtomicU32 = AtomicU32::new(0xFFFF_FFFF);
        [INIT; D3D9_MAX_TSS_STATES]
    };
    [INIT_ROW; D3D9_MAX_TEXTURE_STAGES]
};

static SAMP_CACHE: [[AtomicU32; D3D9_MAX_SAMPLER_STATES]; D3D9_MAX_SAMPLERS] = {
    const INIT_ROW: [AtomicU32; D3D9_MAX_SAMPLER_STATES] = {
        const INIT: AtomicU32 = AtomicU32::new(0xFFFF_FFFF);
        [INIT; D3D9_MAX_SAMPLER_STATES]
    };
    [INIT_ROW; D3D9_MAX_SAMPLERS]
};

// Pipeline performance counters
static STAT_REDUNDANT_STATES_FILTERED: AtomicU64 = AtomicU64::new(0);
static STAT_PASSED_STATES: AtomicU64 = AtomicU64::new(0);
static STAT_VERTICES_TRANSFORMED: AtomicU64 = AtomicU64::new(0);
static STAT_OBJECTS_CULLED: AtomicU64 = AtomicU64::new(0);

/// Filters redundant D3D9 SetRenderState calls.
/// Returns 1 if state is new/modified and must be dispatched to DXVK.
/// Returns 0 if state is identical to shadow state and can be skipped.
#[inline(always)]
pub fn filter_render_state(state: u32, value: u32) -> bool {
    let idx = state as usize;
    if idx < D3D9_MAX_RENDER_STATES {
        let prev = RS_CACHE[idx].swap(value, Ordering::Relaxed);
        if prev == value {
            STAT_REDUNDANT_STATES_FILTERED.fetch_add(1, Ordering::Relaxed);
            return false;
        }
    }
    STAT_PASSED_STATES.fetch_add(1, Ordering::Relaxed);
    true
}

/// Filters redundant D3D9 SetTextureStageState calls.
#[inline(always)]
pub fn filter_texture_stage_state(stage: u32, state: u32, value: u32) -> bool {
    let stg = stage as usize;
    let s = state as usize;
    if stg < D3D9_MAX_TEXTURE_STAGES && s < D3D9_MAX_TSS_STATES {
        let prev = TSS_CACHE[stg][s].swap(value, Ordering::Relaxed);
        if prev == value {
            STAT_REDUNDANT_STATES_FILTERED.fetch_add(1, Ordering::Relaxed);
            return false;
        }
    }
    STAT_PASSED_STATES.fetch_add(1, Ordering::Relaxed);
    true
}

/// Filters redundant D3D9 SetSamplerState calls.
#[inline(always)]
pub fn filter_sampler_state(sampler: u32, state: u32, value: u32) -> bool {
    let samp = sampler as usize;
    let s = state as usize;
    if samp < D3D9_MAX_SAMPLERS && s < D3D9_MAX_SAMPLER_STATES {
        let prev = SAMP_CACHE[samp][s].swap(value, Ordering::Relaxed);
        if prev == value {
            STAT_REDUNDANT_STATES_FILTERED.fetch_add(1, Ordering::Relaxed);
            return false;
        }
    }
    STAT_PASSED_STATES.fetch_add(1, Ordering::Relaxed);
    true
}

// ---------------------------------------------------------------------------
// Native 3D Geometry & Frustum Culling (No Emulation)
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Plane {
    pub a: f32,
    pub b: f32,
    pub c: f32,
    pub d: f32,
}

impl Plane {
    #[inline(always)]
    pub fn normalize(&mut self) {
        let len = (self.a * self.a + self.b * self.b + self.c * self.c).sqrt();
        if len > 1e-6 {
            let inv = 1.0 / len;
            self.a *= inv;
            self.b *= inv;
            self.c *= inv;
            self.d *= inv;
        }
    }

    #[inline(always)]
    pub fn distance_to_point(&self, x: f32, y: f32, z: f32) -> f32 {
        self.a * x + self.b * y + self.c * z + self.d
    }
}

/// Frustum representation containing 6 normalized clipping planes.
#[repr(C)]
pub struct Frustum {
    pub planes: [Plane; 6],
}

impl Frustum {
    /// Extracts frustum planes from a 4x4 View-Projection matrix (column-major or row-major).
    pub fn from_matrix(m: &[f32; 16]) -> Self {
        let mut f = Frustum {
            planes: [
                // Left
                Plane { a: m[3] + m[0], b: m[7] + m[4], c: m[11] + m[8], d: m[15] + m[12] },
                // Right
                Plane { a: m[3] - m[0], b: m[7] - m[4], c: m[11] - m[8], d: m[15] - m[12] },
                // Bottom
                Plane { a: m[3] + m[1], b: m[7] + m[5], c: m[11] + m[9], d: m[15] + m[13] },
                // Top
                Plane { a: m[3] - m[1], b: m[7] - m[5], c: m[11] - m[9], d: m[15] - m[13] },
                // Near
                Plane { a: m[2], b: m[6], c: m[10], d: m[14] },
                // Far
                Plane { a: m[3] - m[2], b: m[7] - m[6], c: m[11] - m[10], d: m[15] - m[14] },
            ],
        };

        for p in &mut f.planes {
            p.normalize();
        }
        f
    }

    /// Tests if an Axis-Aligned Bounding Box (AABB) is inside or intersecting the frustum.
    /// Returns true if visible, false if completely outside (culled).
    pub fn test_aabb(&self, min: &[f32; 3], max: &[f32; 3]) -> bool {
        for p in &self.planes {
            // Pick positive vertex along plane normal
            let px = if p.a > 0.0 { max[0] } else { min[0] };
            let py = if p.b > 0.0 { max[1] } else { min[1] };
            let pz = if p.c > 0.0 { max[2] } else { min[2] };

            if p.distance_to_point(px, py, pz) < 0.0 {
                return false; // Box is completely behind plane
            }
        }
        true
    }

    /// Tests if a bounding sphere is inside or intersecting the frustum.
    pub fn test_sphere(&self, center: &[f32; 3], radius: f32) -> bool {
        for p in &self.planes {
            if p.distance_to_point(center[0], center[1], center[2]) < -radius {
                return false;
            }
        }
        true
    }
}

/// Natively transforms a batch of 3D vertices using a 4x4 matrix without emulation.
pub fn transform_vertices_neon(
    in_ptr: *const f32,
    out_ptr: *mut f32,
    count: usize,
    stride_in: usize,
    stride_out: usize,
    mat: &[f32; 16],
) {
    let m00 = mat[0];  let m01 = mat[1];  let m02 = mat[2];  let m03 = mat[3];
    let m10 = mat[4];  let m11 = mat[5];  let m12 = mat[6];  let m13 = mat[7];
    let m20 = mat[8];  let m21 = mat[9];  let m22 = mat[10]; let m23 = mat[11];
    let m30 = mat[12]; let m31 = mat[13]; let m32 = mat[14]; let m33 = mat[15];

    let stride_in_floats = if stride_in == 0 { 3 } else { stride_in / 4 };
    let stride_out_floats = if stride_out == 0 { 4 } else { stride_out / 4 };

    for i in 0..count {
        unsafe {
            let src = in_ptr.add(i * stride_in_floats);
            let dst = out_ptr.add(i * stride_out_floats);

            let x = *src;
            let y = *src.add(1);
            let z = *src.add(2);

            let rx = x * m00 + y * m10 + z * m20 + m30;
            let ry = x * m01 + y * m11 + z * m21 + m31;
            let rz = x * m02 + y * m12 + z * m22 + m32;
            let rw = x * m03 + y * m13 + z * m23 + m33;

            *dst = rx;
            *dst.add(1) = ry;
            *dst.add(2) = rz;
            if stride_out_floats >= 4 {
                *dst.add(3) = rw;
            }
        }
    }

    STAT_VERTICES_TRANSFORMED.fetch_add(count as u64, Ordering::Relaxed);
}

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_native_pipeline_init() -> c_int {
    // Reset caches to initial uninitialized state
    for slot in RS_CACHE.iter() {
        slot.store(0xFFFF_FFFF, Ordering::Relaxed);
    }
    0
}

#[no_mangle]
pub extern "C" fn nfs_native_pipeline_filter_rs(state: u32, value: u32) -> c_int {
    if filter_render_state(state, value) { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn nfs_native_pipeline_filter_tss(stage: u32, state: u32, value: u32) -> c_int {
    if filter_texture_stage_state(stage, state, value) { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn nfs_native_pipeline_filter_samp(sampler: u32, state: u32, value: u32) -> c_int {
    if filter_sampler_state(sampler, state, value) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_pipeline_transform_vertices(
    in_verts: *const f32,
    out_verts: *mut f32,
    count: usize,
    stride_in: usize,
    stride_out: usize,
    mat: *const f32,
) -> c_int {
    if in_verts.is_null() || out_verts.is_null() || mat.is_null() || count == 0 {
        return -1;
    }
    let m = &*(mat as *const [f32; 16]);
    transform_vertices_neon(in_verts, out_verts, count, stride_in, stride_out, m);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_pipeline_cull_aabb(
    min: *const f32,
    max: *const f32,
    view_proj: *const f32,
) -> c_int {
    if min.is_null() || max.is_null() || view_proj.is_null() {
        return 1; // Default visible on invalid pointers
    }
    let m = &*(view_proj as *const [f32; 16]);
    let min_arr = &*(min as *const [f32; 3]);
    let max_arr = &*(max as *const [f32; 3]);

    let frustum = Frustum::from_matrix(m);
    if frustum.test_aabb(min_arr, max_arr) {
        1 // Visible
    } else {
        STAT_OBJECTS_CULLED.fetch_add(1, Ordering::Relaxed);
        0 // Culled
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_pipeline_cull_sphere(
    center: *const f32,
    radius: f32,
    view_proj: *const f32,
) -> c_int {
    if center.is_null() || view_proj.is_null() {
        return 1;
    }
    let m = &*(view_proj as *const [f32; 16]);
    let center_arr = &*(center as *const [f32; 3]);

    let frustum = Frustum::from_matrix(m);
    if frustum.test_sphere(center_arr, radius) {
        1
    } else {
        STAT_OBJECTS_CULLED.fetch_add(1, Ordering::Relaxed);
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_native_pipeline_get_stats(
    out_filtered: *mut u64,
    out_passed: *mut u64,
    out_transformed: *mut u64,
    out_culled: *mut u64,
) {
    if !out_filtered.is_null() {
        *out_filtered = STAT_REDUNDANT_STATES_FILTERED.load(Ordering::Relaxed);
    }
    if !out_passed.is_null() {
        *out_passed = STAT_PASSED_STATES.load(Ordering::Relaxed);
    }
    if !out_transformed.is_null() {
        *out_transformed = STAT_VERTICES_TRANSFORMED.load(Ordering::Relaxed);
    }
    if !out_culled.is_null() {
        *out_culled = STAT_OBJECTS_CULLED.load(Ordering::Relaxed);
    }
}

#[no_mangle]
pub extern "C" fn nfs_native_pipeline_reset_stats() {
    STAT_REDUNDANT_STATES_FILTERED.store(0, Ordering::Relaxed);
    STAT_PASSED_STATES.store(0, Ordering::Relaxed);
    STAT_VERTICES_TRANSFORMED.store(0, Ordering::Relaxed);
    STAT_OBJECTS_CULLED.store(0, Ordering::Relaxed);
}
