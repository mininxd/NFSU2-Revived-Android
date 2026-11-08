//! High-Performance SIMD & NEON Vector/Matrix Math Library in Rust for NFSU2 Android.
//!
//! Phase 1 Native Translation: Replaces emulated x87 FPU stack operations in the game's
//! vehicle physics, collision detection, and world transformation pipelines with native
//! 128-bit ARM64 NEON and portable SIMD operations.

use std::slice;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0 };

    #[inline(always)]
    pub fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vec4 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl Vec4 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0, w: 0.0 };

    #[inline(always)]
    pub fn new(x: f32, y: f32, z: f32, w: f32) -> Self {
        Self { x, y, z, w }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Mat4 {
    pub m: [f32; 16],
}

impl Mat4 {
    pub const IDENTITY: Self = Self {
        m: [
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        ],
    };
}

// ---------------------------------------------------------------------------
// 3D Vector Math Operations
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn dot_vec3(a: &Vec3, b: &Vec3) -> f32 {
    a.x * b.x + a.y * b.y + a.z * b.z
}

#[inline(always)]
pub fn cross_vec3(a: &Vec3, b: &Vec3) -> Vec3 {
    Vec3 {
        x: a.y * b.z - a.z * b.y,
        y: a.z * b.x - a.x * b.z,
        z: a.x * b.y - a.y * b.x,
    }
}

#[inline(always)]
pub fn length_vec3(v: &Vec3) -> f32 {
    dot_vec3(v, v).sqrt()
}

#[inline(always)]
pub fn normalize_vec3(v: &Vec3) -> Vec3 {
    let len = length_vec3(v);
    if len > 1e-6 {
        let inv = 1.0 / len;
        Vec3 {
            x: v.x * inv,
            y: v.y * inv,
            z: v.z * inv,
        }
    } else {
        Vec3::ZERO
    }
}

// ---------------------------------------------------------------------------
// 4x4 Matrix Multiply with ARM64 NEON Optimization
// ---------------------------------------------------------------------------

#[cfg(target_arch = "aarch64")]
#[inline(always)]
pub unsafe fn mat4_mul_neon(a: &[f32; 16], b: &[f32; 16], out: &mut [f32; 16]) {
    use core::arch::aarch64::*;

    let b0 = vld1q_f32(b.as_ptr().add(0));
    let b1 = vld1q_f32(b.as_ptr().add(4));
    let b2 = vld1q_f32(b.as_ptr().add(8));
    let b3 = vld1q_f32(b.as_ptr().add(12));

    for i in 0..4 {
        let row_idx = i * 4;
        let ai0 = vdupq_n_f32(*a.get_unchecked(row_idx + 0));
        let ai1 = vdupq_n_f32(*a.get_unchecked(row_idx + 1));
        let ai2 = vdupq_n_f32(*a.get_unchecked(row_idx + 2));
        let ai3 = vdupq_n_f32(*a.get_unchecked(row_idx + 3));

        let mut acc = vmulq_f32(ai0, b0);
        acc = vfmaq_f32(acc, ai1, b1);
        acc = vfmaq_f32(acc, ai2, b2);
        acc = vfmaq_f32(acc, ai3, b3);

        vst1q_f32(out.as_mut_ptr().add(row_idx), acc);
    }
}

#[inline(always)]
pub fn mat4_mul_scalar(a: &[f32; 16], b: &[f32; 16], out: &mut [f32; 16]) {
    for i in 0..4 {
        for j in 0..4 {
            let mut sum = 0.0f32;
            for k in 0..4 {
                sum += a[i * 4 + k] * b[k * 4 + j];
            }
            out[i * 4 + j] = sum;
        }
    }
}

// ---------------------------------------------------------------------------
// Vehicle Physics & Collision Math
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Aabb {
    pub min: Vec3,
    pub max: Vec3,
}

#[inline(always)]
pub fn aabb_overlaps(a: &Aabb, b: &Aabb) -> bool {
    a.min.x <= b.max.x && a.max.x >= b.min.x &&
    a.min.y <= b.max.y && a.max.y >= b.min.y &&
    a.min.z <= b.max.z && a.max.z >= b.min.z
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SuspensionRay {
    pub origin: Vec3,
    pub dir: Vec3,
    pub rest_length: f32,
    pub spring_k: f32,
    pub damper_c: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SuspensionResult {
    pub hit: bool,
    pub compression: f32, // 0.0 = fully extended, 1.0 = bottomed out
    pub spring_force: f32,
    pub contact_normal: Vec3,
}

/// Computes car suspension raycast reaction against ground height plane.
#[inline(always)]
pub fn compute_suspension_reaction(
    ray: &SuspensionRay,
    ground_height: f32,
    wheel_speed: f32,
) -> SuspensionResult {
    // Ground intersection along down-vector (dir.y should be negative)
    if ray.dir.y.abs() < 1e-5 {
        return SuspensionResult {
            hit: false,
            compression: 0.0,
            spring_force: 0.0,
            contact_normal: Vec3::new(0.0, 1.0, 0.0),
        };
    }

    let t = (ground_height - ray.origin.y) / ray.dir.y;
    if t >= 0.0 && t <= ray.rest_length {
        let compression = 1.0 - (t / ray.rest_length);
        let displacement = ray.rest_length - t;
        let spring = displacement * ray.spring_k;
        let damping = wheel_speed * ray.damper_c;
        let total_force = (spring - damping).max(0.0);

        SuspensionResult {
            hit: true,
            compression,
            spring_force: total_force,
            contact_normal: Vec3::new(0.0, 1.0, 0.0),
        }
    } else {
        SuspensionResult {
            hit: false,
            compression: 0.0,
            spring_force: 0.0,
            contact_normal: Vec3::new(0.0, 1.0, 0.0),
        }
    }
}

// ---------------------------------------------------------------------------
// C-ABI Exports for Game Integration
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_simd_vec3_dot(a: *const Vec3, b: *const Vec3) -> f32 {
    if a.is_null() || b.is_null() { return 0.0; }
    dot_vec3(&*a, &*b)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_simd_vec3_cross(a: *const Vec3, b: *const Vec3, out: *mut Vec3) {
    if a.is_null() || b.is_null() || out.is_null() { return; }
    *out = cross_vec3(&*a, &*b);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_simd_vec3_normalize(v: *const Vec3, out: *mut Vec3) {
    if v.is_null() || out.is_null() { return; }
    *out = normalize_vec3(&*v);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_simd_mat4_mul(a: *const f32, b: *const f32, out: *mut f32) {
    if a.is_null() || b.is_null() || out.is_null() { return; }
    let a_ref = &*(a as *const [f32; 16]);
    let b_ref = &*(b as *const [f32; 16]);
    let out_ref = &mut *(out as *mut [f32; 16]);

    #[cfg(target_arch = "aarch64")]
    {
        mat4_mul_neon(a_ref, b_ref, out_ref);
    }
    #[cfg(not(target_arch = "aarch64"))]
    {
        mat4_mul_scalar(a_ref, b_ref, out_ref);
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_simd_aabb_intersect(a: *const Aabb, b: *const Aabb) -> i32 {
    if a.is_null() || b.is_null() { return 0; }
    if aabb_overlaps(&*a, &*b) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_simd_suspension_step(
    ray: *const SuspensionRay,
    ground_height: f32,
    wheel_speed: f32,
    out_result: *mut SuspensionResult,
) {
    if ray.is_null() || out_result.is_null() { return; }
    *out_result = compute_suspension_reaction(&*ray, ground_height, wheel_speed);
}
