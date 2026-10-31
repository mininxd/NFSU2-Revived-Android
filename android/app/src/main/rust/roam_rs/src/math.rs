//! Deterministic mathematical functions and quaternion rotation utilities.

use std::f32::consts::PI;

#[inline]
pub fn f_finite(f: f32) -> bool {
    (f.to_bits() & 0x7F80_0000) != 0x7F80_0000
}

#[inline]
pub fn f_clamp(f: f32, lo: f32, hi: f32) -> f32 {
    if f < lo {
        lo
    } else if f > hi {
        hi
    } else {
        f
    }
}

#[inline]
pub fn f_round(f: f32) -> i32 {
    if f.is_nan() {
        return 0;
    }
    if f >= 2147483520.0 {
        return i32::MAX;
    }
    if f <= -2147483520.0 {
        return i32::MIN;
    }
    (if f >= 0.0 { f + 0.5 } else { f - 0.5 }) as i32
}

pub fn q_norm(q: &mut [f32; 4]) {
    let n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if n < 1e-12 {
        q[0] = 0.0;
        q[1] = 0.0;
        q[2] = 0.0;
        q[3] = 1.0;
        return;
    }
    let k = 1.0 / n.sqrt();
    for v in q.iter_mut() {
        *v *= k;
    }
}

pub fn q_mul(a: &[f32; 4], b: &[f32; 4], o: &mut [f32; 4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

pub fn q_nlerp(a: &[f32; 4], b: &[f32; 4], t: f32, o: &mut [f32; 4]) {
    let d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    let s = if d < 0.0 { -1.0 } else { 1.0 };
    for i in 0..4 {
        o[i] = (1.0 - t) * a[i] + t * s * b[i];
    }
    q_norm(o);
}

#[no_mangle]
pub extern "C" fn rc_expneg(x: f32) -> f32 {
    if !(x > 0.0) {
        return 1.0;
    }
    if x > 30.0 {
        return 0.0;
    }
    let n = x as i32;
    let f = x - n as f32;
    let e = 1.0 + f * (1.0 + f * (0.5 + f * (1.0 / 6.0 + f * (1.0 / 24.0 + f * (1.0 / 120.0 + f * (1.0 / 720.0 + f * (1.0 / 5040.0 + f * (1.0 / 40320.0))))))));
    let mut r = 1.0 / e;
    for _ in 0..n {
        r *= 0.36787944117;
    }
    r
}

#[no_mangle]
pub extern "C" fn rc_atan2f(y: f32, x: f32) -> f32 {
    let ax = x.abs();
    let ay = y.abs();
    if !(ax > 0.0) && !(ay > 0.0) {
        return 0.0;
    }
    let swap = ay > ax;
    let mut t = if swap { ax / ay } else { ay / ax };
    let mut base = 0.0;
    if t > 0.26794919243 {
        t = (t * 1.73205080757 - 1.0) / (t + 1.73205080757);
        base = PI / 6.0;
    }
    let t2 = t * t;
    let mut a = t * (1.0 + t2 * (-1.0 / 3.0 + t2 * (1.0 / 5.0 + t2 * (-1.0 / 7.0 + t2 * (1.0 / 9.0 + t2 * (-1.0 / 11.0 + t2 * (1.0 / 13.0)))))));
    a += base;
    if swap {
        a = (PI / 2.0) - a;
    }
    if x < 0.0 {
        a = PI - a;
    }
    if y < 0.0 { -a } else { a }
}

#[no_mangle]
pub extern "C" fn rc_m2q(f: *const f32, l: *const f32, u: *const f32, q: *mut f32) {
    if f.is_null() || l.is_null() || u.is_null() || q.is_null() {
        return;
    }
    unsafe {
        let f_s = std::slice::from_raw_parts(f, 3);
        let l_s = std::slice::from_raw_parts(l, 3);
        let u_s = std::slice::from_raw_parts(u, 3);
        let q_s = std::slice::from_raw_parts_mut(q, 4);

        let tr = f_s[0] + l_s[1] + u_s[2];
        if tr > 0.0 {
            let s = (tr + 1.0).sqrt() * 2.0;
            q_s[3] = 0.25 * s;
            q_s[0] = (l_s[2] - u_s[1]) / s;
            q_s[1] = (u_s[0] - f_s[2]) / s;
            q_s[2] = (f_s[1] - l_s[0]) / s;
        } else if f_s[0] > l_s[1] && f_s[0] > u_s[2] {
            let s = (1.0 + f_s[0] - l_s[1] - u_s[2]).sqrt() * 2.0;
            q_s[3] = (l_s[2] - u_s[1]) / s;
            q_s[0] = 0.25 * s;
            q_s[1] = (l_s[0] + f_s[1]) / s;
            q_s[2] = (u_s[0] + f_s[2]) / s;
        } else if l_s[1] > u_s[2] {
            let s = (1.0 + l_s[1] - f_s[0] - u_s[2]).sqrt() * 2.0;
            q_s[3] = (u_s[0] - f_s[2]) / s;
            q_s[0] = (l_s[0] + f_s[1]) / s;
            q_s[1] = 0.25 * s;
            q_s[2] = (u_s[1] + l_s[2]) / s;
        } else {
            let s = (1.0 + u_s[2] - f_s[0] - l_s[1]).sqrt() * 2.0;
            q_s[3] = (f_s[1] - l_s[0]) / s;
            q_s[0] = (u_s[0] + f_s[2]) / s;
            q_s[1] = (u_s[1] + l_s[2]) / s;
            q_s[2] = 0.25 * s;
        }
        let mut q_arr = [q_s[0], q_s[1], q_s[2], q_s[3]];
        q_norm(&mut q_arr);
        q_s.copy_from_slice(&q_arr);
    }
}

#[no_mangle]
pub extern "C" fn rc_q2m(q: *const f32, f: *mut f32, l: *mut f32, u: *mut f32) {
    if q.is_null() || f.is_null() || l.is_null() || u.is_null() {
        return;
    }
    unsafe {
        let q_s = std::slice::from_raw_parts(q, 4);
        let f_s = std::slice::from_raw_parts_mut(f, 3);
        let l_s = std::slice::from_raw_parts_mut(l, 3);
        let u_s = std::slice::from_raw_parts_mut(u, 3);

        let x = q_s[0];
        let y = q_s[1];
        let z = q_s[2];
        let w = q_s[3];

        f_s[0] = 1.0 - 2.0 * (y * y + z * z);
        f_s[1] = 2.0 * (x * y + z * w);
        f_s[2] = 2.0 * (x * z - y * w);

        l_s[0] = 2.0 * (x * y - z * w);
        l_s[1] = 1.0 - 2.0 * (x * x + z * z);
        l_s[2] = 2.0 * (y * z + x * w);

        u_s[0] = 2.0 * (x * z + y * w);
        u_s[1] = 2.0 * (y * z - x * w);
        u_s[2] = 1.0 - 2.0 * (x * x + y * y);
    }
}
