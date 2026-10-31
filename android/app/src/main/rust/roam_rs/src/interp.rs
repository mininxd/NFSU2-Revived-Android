//! Pure Rust vehicle telemetry interpolation, dead-reckoning extrapolation,
//! and frame-pacing simulation rate filter.

use crate::math::{f_clamp, q_nlerp, q_norm};

/// 3D Vector for vehicle positioning and velocities.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0 };

    #[inline]
    pub fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    #[inline]
    pub fn length_squared(&self) -> f32 {
        self.x * self.x + self.y * self.y + self.z * self.z
    }

    #[inline]
    pub fn length(&self) -> f32 {
        self.length_squared().sqrt()
    }

    #[inline]
    pub fn lerp(a: Self, b: Self, t: f32) -> Self {
        let t = f_clamp(t, 0.0, 1.0);
        Self {
            x: a.x + (b.x - a.x) * t,
            y: a.y + (b.y - a.y) * t,
            z: a.z + (b.z - a.z) * t,
        }
    }

    /// Cubic Hermite spline interpolation between points (p0, p1) with tangent velocities (v0, v1).
    pub fn hermite(p0: Self, v0: Self, p1: Self, v1: Self, t: f32) -> Self {
        let t = f_clamp(t, 0.0, 1.0);
        let t2 = t * t;
        let t3 = t2 * t;

        let h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        let h10 = t3 - 2.0 * t2 + t;
        let h01 = -2.0 * t3 + 3.0 * t2;
        let h11 = t3 - t2;

        Self {
            x: h00 * p0.x + h10 * v0.x + h01 * p1.x + h11 * v1.x,
            y: h00 * p0.y + h10 * v0.y + h01 * p1.y + h11 * v1.y,
            z: h00 * p0.z + h10 * v0.z + h01 * p1.z + h11 * v1.z,
        }
    }
}

/// Quaternion orientation [x, y, z, w].
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Quat {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl Quat {
    pub const IDENTITY: Self = Self { x: 0.0, y: 0.0, z: 0.0, w: 1.0 };

    #[inline]
    pub fn new(x: f32, y: f32, z: f32, w: f32) -> Self {
        Self { x, y, z, w }
    }

    #[inline]
    pub fn dot(&self, other: &Self) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z + self.w * other.w
    }

    /// Spherical Linear Interpolation (slerp) between orientations.
    pub fn slerp(q0: Self, mut q1: Self, t: f32) -> Self {
        let t = f_clamp(t, 0.0, 1.0);
        let mut dot = q0.dot(&q1);

        // If negative, negate one quaternion to take the shortest path on the 4D sphere
        if dot < 0.0 {
            q1.x = -q1.x;
            q1.y = -q1.y;
            q1.z = -q1.z;
            q1.w = -q1.w;
            dot = -dot;
        }

        if dot > 0.9995 {
            // Close enough: use normalized linear interpolation to avoid division by zero
            let mut out = [0.0f32; 4];
            let a = [q0.x, q0.y, q0.z, q0.w];
            let b = [q1.x, q1.y, q1.z, q1.w];
            q_nlerp(&a, &b, t, &mut out);
            return Self::new(out[0], out[1], out[2], out[3]);
        }

        let theta_0 = dot.acos();
        let theta = theta_0 * t;
        let sin_theta = theta.sin();
        let sin_theta_0 = theta_0.sin();

        let s0 = (theta_0 - theta).sin() / sin_theta_0;
        let s1 = sin_theta / sin_theta_0;

        let mut res = Self {
            x: (s0 * q0.x) + (s1 * q1.x),
            y: (s0 * q0.y) + (s1 * q1.y),
            z: (s0 * q0.z) + (s1 * q1.z),
            w: (s0 * q0.w) + (s1 * q1.w),
        };
        let mut arr = [res.x, res.y, res.z, res.w];
        q_norm(&mut arr);
        res.x = arr[0];
        res.y = arr[1];
        res.z = arr[2];
        res.w = arr[3];
        res
    }
}

/// Dead-reckoning extrapolator: advances vehicle transform according to linear
/// and angular velocities with damping.
pub fn dead_reckon_step(
    pos: &mut Vec3,
    vel: &mut Vec3,
    rot: &mut Quat,
    ang_vel: &Vec3,
    dt: f32,
    lin_drag: f32,
    ang_drag: f32,
) {
    if dt <= 0.0 || dt > 0.5 {
        return;
    }

    // Position integration: x(t + dt) = x(t) + v(t) * dt
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.z += vel.z * dt;

    // Linear velocity damping: v = v * (1 - drag * dt)
    let lin_factor = (1.0 - lin_drag * dt).max(0.0);
    vel.x *= lin_factor;
    vel.y *= lin_factor;
    vel.z *= lin_factor;

    // Angular orientation integration
    let w_len = ang_vel.length();
    if w_len > 1e-6 {
        let half_angle = 0.5 * w_len * dt;
        let sin_half = half_angle.sin() / w_len;
        let dq = [
            ang_vel.x * sin_half,
            ang_vel.y * sin_half,
            ang_vel.z * sin_half,
            half_angle.cos(),
        ];
        let mut new_rot = [0.0f32; 4];
        crate::math::q_mul(&[rot.x, rot.y, rot.z, rot.w], &dq, &mut new_rot);
        q_norm(&mut new_rot);
        rot.x = new_rot[0];
        rot.y = new_rot[1];
        rot.z = new_rot[2];
        rot.w = new_rot[3];
    }
}

/// Wheel telemetry: suspension travel [FL, FR, RL, RR] and steer angle.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct WheelTelemetry {
    pub suspension: [f32; 4],
    pub steer_angle: f32,
    pub wheel_rot: [f32; 4],
}

impl WheelTelemetry {
    pub fn lerp(&self, other: &Self, t: f32) -> Self {
        let t = f_clamp(t, 0.0, 1.0);
        let mut susp = [0.0f32; 4];
        let mut wrot = [0.0f32; 4];
        for i in 0..4 {
            susp[i] = self.suspension[i] + (other.suspension[i] - self.suspension[i]) * t;
            wrot[i] = self.wheel_rot[i] + (other.wheel_rot[i] - self.wheel_rot[i]) * t;
        }
        let steer = self.steer_angle + (other.steer_angle - self.steer_angle) * t;
        Self {
            suspension: susp,
            steer_angle: steer,
            wheel_rot: wrot,
        }
    }
}

/// Anti-slowmotion delta-time filter: checks whether the engine's internal
/// simulation time step was clamped below real elapsed wall-clock time.
/// Returns the corrected real-time delta if clamped, or the original dt.
#[inline]
pub fn pacing_filter_dt(real_elapsed: f32, current_wdt: f32) -> f32 {
    if real_elapsed > 0.010 && real_elapsed < 0.150 {
        if current_wdt > 0.0 && current_wdt < real_elapsed * 0.90 {
            // Clamped by the engine's 50 ms cap: advance simulation by real elapsed time
            return real_elapsed;
        }
    }
    current_wdt
}

// -----------------------------------------------------------------------------
// C FFI Interface
// -----------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_interp_vec3_lerp(
    a: *const f32,
    b: *const f32,
    t: f32,
    out: *mut f32,
) {
    if a.is_null() || b.is_null() || out.is_null() {
        return;
    }
    let va = Vec3::new(*a, *a.add(1), *a.add(2));
    let vb = Vec3::new(*b, *b.add(1), *b.add(2));
    let res = Vec3::lerp(va, vb, t);
    *out = res.x;
    *out.add(1) = res.y;
    *out.add(2) = res.z;
}

#[no_mangle]
pub unsafe extern "C" fn nfs_interp_vec3_hermite(
    p0: *const f32,
    v0: *const f32,
    p1: *const f32,
    v1: *const f32,
    t: f32,
    out: *mut f32,
) {
    if p0.is_null() || v0.is_null() || p1.is_null() || v1.is_null() || out.is_null() {
        return;
    }
    let vp0 = Vec3::new(*p0, *p0.add(1), *p0.add(2));
    let vv0 = Vec3::new(*v0, *v0.add(1), *v0.add(2));
    let vp1 = Vec3::new(*p1, *p1.add(1), *p1.add(2));
    let vv1 = Vec3::new(*v1, *v1.add(1), *v1.add(2));
    let res = Vec3::hermite(vp0, vv0, vp1, vv1, t);
    *out = res.x;
    *out.add(1) = res.y;
    *out.add(2) = res.z;
}

#[no_mangle]
pub unsafe extern "C" fn nfs_interp_quat_slerp(
    q0: *const f32,
    q1: *const f32,
    t: f32,
    out: *mut f32,
) {
    if q0.is_null() || q1.is_null() || out.is_null() {
        return;
    }
    let vq0 = Quat::new(*q0, *q0.add(1), *q0.add(2), *q0.add(3));
    let vq1 = Quat::new(*q1, *q1.add(1), *q1.add(2), *q1.add(3));
    let res = Quat::slerp(vq0, vq1, t);
    *out = res.x;
    *out.add(1) = res.y;
    *out.add(2) = res.z;
    *out.add(3) = res.w;
}

#[no_mangle]
pub unsafe extern "C" fn nfs_interp_dead_reckon(
    pos: *mut f32,
    vel: *mut f32,
    rot: *mut f32,
    ang_vel: *const f32,
    dt: f32,
    lin_drag: f32,
    ang_drag: f32,
) {
    if pos.is_null() || vel.is_null() || rot.is_null() || ang_vel.is_null() {
        return;
    }
    let mut vpos = Vec3::new(*pos, *pos.add(1), *pos.add(2));
    let mut vvel = Vec3::new(*vel, *vel.add(1), *vel.add(2));
    let mut vrot = Quat::new(*rot, *rot.add(1), *rot.add(2), *rot.add(3));
    let vang = Vec3::new(*ang_vel, *ang_vel.add(1), *ang_vel.add(2));

    dead_reckon_step(&mut vpos, &mut vvel, &mut vrot, &vang, dt, lin_drag, ang_drag);

    *pos = vpos.x;
    *pos.add(1) = vpos.y;
    *pos.add(2) = vpos.z;

    *vel = vvel.x;
    *vel.add(1) = vvel.y;
    *vel.add(2) = vvel.z;

    *rot = vrot.x;
    *rot.add(1) = vrot.y;
    *rot.add(2) = vrot.z;
    *rot.add(3) = vrot.w;
}

#[no_mangle]
pub unsafe extern "C" fn nfs_interp_wheels(
    w0: *const WheelTelemetry,
    w1: *const WheelTelemetry,
    t: f32,
    out: *mut WheelTelemetry,
) {
    if w0.is_null() || w1.is_null() || out.is_null() {
        return;
    }
    let res = (*w0).lerp(&*w1, t);
    *out = res;
}

#[no_mangle]
pub extern "C" fn nfs_pacing_filter_dt(real_elapsed: f32, current_wdt: f32) -> f32 {
    pacing_filter_dt(real_elapsed, current_wdt)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_vec3_lerp() {
        let a = Vec3::new(0.0, 10.0, 20.0);
        let b = Vec3::new(10.0, 20.0, 40.0);
        let mid = Vec3::lerp(a, b, 0.5);
        assert!((mid.x - 5.0).abs() < 1e-5);
        assert!((mid.y - 15.0).abs() < 1e-5);
        assert!((mid.z - 30.0).abs() < 1e-5);
    }

    #[test]
    fn test_quat_slerp() {
        let q0 = Quat::IDENTITY;
        let q1 = Quat::new(0.0, 1.0, 0.0, 0.0);
        let mid = Quat::slerp(q0, q1, 0.5);
        assert!((mid.dot(&mid) - 1.0).abs() < 1e-4);
    }

    #[test]
    fn test_pacing_filter() {
        // Clamped dt (50 ms) while real elapsed took 70 ms: should return 70 ms
        let filtered = pacing_filter_dt(0.070, 0.050);
        assert!((filtered - 0.070).abs() < 1e-5);

        // Normal dt (16.6 ms) while real elapsed took 16.6 ms: should return original
        let normal = pacing_filter_dt(0.0166, 0.0166);
        assert!((normal - 0.0166).abs() < 1e-5);
    }
}
