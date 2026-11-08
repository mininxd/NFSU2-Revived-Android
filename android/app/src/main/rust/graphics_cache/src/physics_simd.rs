//! Phase 2 NEON SIMD Physics Acceleration Engine in Rust for NFSU2 Android.
//!
//! Provides vectorized 4-wheel vehicle dynamics, batch Pacejka friction circle evaluation,
//! 4-corner suspension reaction steps, and high-speed aerodynamic downforce calculations
//! using ARM64 NEON 128-bit vector intrinsics.

#[cfg(target_arch = "aarch64")]
use core::arch::aarch64::*;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Suspension4Corner {
    pub rest_length: [f32; 4],
    pub spring_k: [f32; 4],
    pub damper_c: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Suspension4Result {
    pub compression: [f32; 4],
    pub force: [f32; 4],
}

/// Evaluates Pacejka friction forces for 4 wheels simultaneously.
///
/// Uses ARM64 NEON vector lanes when running on aarch64.
#[inline(always)]
pub unsafe fn eval_4wheel_friction_neon(
    normal_loads: &[f32; 4],
    slip_ratios: &[f32; 4],
    slip_angles: &[f32; 4],
    friction_coeffs: &[f32; 4],
    out_fx: &mut [f32; 4],
    out_fy: &mut [f32; 4],
) {
    #[cfg(target_arch = "aarch64")]
    {
        let v_fz = vld1q_f32(normal_loads.as_ptr());
        let v_sr = vld1q_f32(slip_ratios.as_ptr());
        let v_sa = vld1q_f32(slip_angles.as_ptr());
        let v_mu = vld1q_f32(friction_coeffs.as_ptr());

        // Max traction force limit per tire: F_max = mu * F_z
        let v_fmax = vmulq_f32(v_mu, v_fz);

        // Approximate linear tire model with saturation:
        // Fx = clamp(Sr * 10.0, -1.0, 1.0) * F_max
        // Fy = clamp(Sa * 8.0, -1.0, 1.0) * F_max
        let v_ten = vdupq_n_f32(10.0);
        let v_eight = vdupq_n_f32(8.0);
        let v_one = vdupq_n_f32(1.0);
        let v_neg_one = vdupq_n_f32(-1.0);

        let v_kx = vminq_f32(v_one, vmaxq_f32(v_neg_one, vmulq_f32(v_sr, v_ten)));
        let v_ky = vminq_f32(v_one, vmaxq_f32(v_neg_one, vmulq_f32(v_sa, v_eight)));

        let mut v_fx = vmulq_f32(v_kx, v_fmax);
        let mut v_fy = vmulq_f32(v_ky, v_fmax);

        // Friction circle combination: normalize if fx^2 + fy^2 > fmax^2
        let v_fx2 = vmulq_f32(v_fx, v_fx);
        let v_fy2 = vmulq_f32(v_fy, v_fy);
        let v_fsq = vaddq_f32(v_fx2, v_fy2);
        let v_fmax2 = vmulq_f32(v_fmax, v_fmax);

        // Fast reciprocal square root for combined friction circle scaling
        let v_rsqrt = vrsqrteq_f32(v_fsq);
        let v_scale = vminq_f32(v_one, vmulq_f32(v_fmax, v_rsqrt));

        v_fx = vmulq_f32(v_fx, v_scale);
        v_fy = vmulq_f32(v_fy, v_scale);

        vst1q_f32(out_fx.as_mut_ptr(), v_fx);
        vst1q_f32(out_fy.as_mut_ptr(), v_fy);
    }
    #[cfg(not(target_arch = "aarch64"))]
    {
        for i in 0..4 {
            let fmax = (friction_coeffs[i] * normal_loads[i]).max(0.0);
            let kx = (slip_ratios[i] * 10.0).clamp(-1.0, 1.0);
            let ky = (slip_angles[i] * 8.0).clamp(-1.0, 1.0);

            let mut fx = kx * fmax;
            let mut fy = ky * fmax;

            let mag = (fx * fx + fy * fy).sqrt();
            if mag > fmax && mag > 1e-4 {
                let s = fmax / mag;
                fx *= s;
                fy *= s;
            }

            out_fx[i] = fx;
            out_fy[i] = fy;
        }
    }
}

/// Evaluates suspension spring & damper reactions for 4 wheel corners simultaneously.
#[inline(always)]
pub unsafe fn eval_4wheel_suspension_neon(
    corners: &Suspension4Corner,
    displacements: &[f32; 4],
    wheel_speeds: &[f32; 4],
    out: &mut Suspension4Result,
) {
    #[cfg(target_arch = "aarch64")]
    {
        let v_rest = vld1q_f32(corners.rest_length.as_ptr());
        let v_k = vld1q_f32(corners.spring_k.as_ptr());
        let v_c = vld1q_f32(corners.damper_c.as_ptr());
        let v_disp = vld1q_f32(displacements.as_ptr());
        let v_spd = vld1q_f32(wheel_speeds.as_ptr());
        let v_zero = vdupq_n_f32(0.0);
        let v_one = vdupq_n_f32(1.0);

        // Compression = clamp(disp / rest, 0.0, 1.0)
        let v_comp = vminq_f32(v_one, vmaxq_f32(v_zero, vdivq_f32(v_disp, v_rest)));

        // Force = clamp(disp * k - spd * c, 0.0, inf)
        let v_spring = vmulq_f32(v_disp, v_k);
        let v_damper = vmulq_f32(v_spd, v_c);
        let v_force = vmaxq_f32(v_zero, vsubq_f32(v_spring, v_damper));

        vst1q_f32(out.compression.as_mut_ptr(), v_comp);
        vst1q_f32(out.force.as_mut_ptr(), v_force);
    }
    #[cfg(not(target_arch = "aarch64"))]
    {
        for i in 0..4 {
            let rest = corners.rest_length[i].max(1e-4);
            let comp = (displacements[i] / rest).clamp(0.0, 1.0);
            let spring = displacements[i] * corners.spring_k[i];
            let damper = wheel_speeds[i] * corners.damper_c[i];
            let force = (spring - damper).max(0.0);

            out.compression[i] = comp;
            out.force[i] = force;
        }
    }
}

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

/// Computes Pacejka 4-wheel friction circle forces using SIMD.
#[no_mangle]
pub unsafe extern "C" fn nfs_simd_4wheel_friction(
    normal_loads: *const f32,
    slip_ratios: *const f32,
    slip_angles: *const f32,
    friction_coeffs: *const f32,
    out_fx: *mut f32,
    out_fy: *mut f32,
) {
    if normal_loads.is_null()
        || slip_ratios.is_null()
        || slip_angles.is_null()
        || friction_coeffs.is_null()
        || out_fx.is_null()
        || out_fy.is_null()
    {
        return;
    }

    eval_4wheel_friction_neon(
        &*(normal_loads as *const [f32; 4]),
        &*(slip_ratios as *const [f32; 4]),
        &*(slip_angles as *const [f32; 4]),
        &*(friction_coeffs as *const [f32; 4]),
        &mut *(out_fx as *mut [f32; 4]),
        &mut *(out_fy as *mut [f32; 4]),
    );
}

/// Computes 4-corner suspension reactions in a single SIMD pass.
#[no_mangle]
pub unsafe extern "C" fn nfs_simd_4wheel_suspension(
    corners: *const Suspension4Corner,
    displacements: *const f32,
    wheel_speeds: *const f32,
    out: *mut Suspension4Result,
) {
    if corners.is_null() || displacements.is_null() || wheel_speeds.is_null() || out.is_null() {
        return;
    }

    eval_4wheel_suspension_neon(
        &*corners,
        &*(displacements as *const [f32; 4]),
        &*(wheel_speeds as *const [f32; 4]),
        &mut *out,
    );
}

/// Computes high-speed aerodynamic downforce and drag forces.
#[no_mangle]
pub unsafe extern "C" fn nfs_simd_aero_forces(
    forward_vel: f32,
    air_density: f32,
    downforce_coeff: f32,
    drag_coeff: f32,
    frontal_area: f32,
    out_downforce: *mut f32,
    out_drag: *mut f32,
) {
    let q = 0.5 * air_density * forward_vel * forward_vel * frontal_area;
    if !out_downforce.is_null() {
        *out_downforce = q * downforce_coeff;
    }
    if !out_drag.is_null() {
        *out_drag = q * drag_coeff;
    }
}
