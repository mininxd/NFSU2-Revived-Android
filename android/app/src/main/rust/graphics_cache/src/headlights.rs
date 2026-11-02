//! Overpass Headlight Height Cutoff & Road Plane Projector Filter.
//!
//! When cars drive over bridges or elevated overpasses, their projected headlights
//! should not pass down through the concrete structure and light the road below.
//!
//! The Z column of the light projector matrix T stores the height of the illuminated
//! point relative to the car's road plane: (X - P_k) . U_k
//! The shader then attenuates:
//! road = saturate((p.z + 3.5 + 0.06 * p.w) / 1.5)
//!
//! Under an overpass (height diff >= 6m), road becomes 0.0, preventing ghost headlight pools.

#[inline]
pub fn compute_road_plane_normal_dist(
    car_rot_up: [f32; 3],
    car_pos: [f32; 3],
) -> [f32; 4] {
    let p_dot_u = car_pos[0] * car_rot_up[0]
        + car_pos[1] * car_rot_up[1]
        + car_pos[2] * car_rot_up[2];
    [car_rot_up[0], car_rot_up[1], car_rot_up[2], -p_dot_u]
}

/// Applies the road plane normal into column 2 of the light projection matrix T.
///
/// `ck_m`: Car transform matrix (row 2 is up axis, row 3 is position)
/// `mi_m`: Inverse projector matrix
/// `t_m`: Target projector matrix (column 2 will be overwritten)
pub fn apply_road_plane_to_projector(
    ck_m: &[[f32; 4]; 4],
    mi_m: &[[f32; 4]; 4],
    t_m: &mut [[f32; 4]; 4],
) {
    let up = [ck_m[2][0], ck_m[2][1], ck_m[2][2]];
    let pos = [ck_m[3][0], ck_m[3][1], ck_m[3][2]];
    let g = compute_road_plane_normal_dist(up, pos);

    for r in 0..4 {
        let mut s = 0.0f32;
        for a in 0..4 {
            s += mi_m[r][a] * g[a];
        }
        t_m[r][2] = s;
    }
}

/// Computes the headlight road attenuation factor from height `pz` and distance `pw`.
/// Returns 0.0 (completely culled) to 1.0 (fully lit).
#[inline]
pub fn compute_headlight_attenuation(pz: f32, pw: f32) -> f32 {
    let road = (pz + 3.5 + 0.06 * pw) / 1.5;
    road.clamp(0.0, 1.0)
}

// ---------------------------------------------------------------------------
// C-ABI Exported Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_headlight_apply_road_plane(
    ck_ptr: *const f32,
    mi_ptr: *const f32,
    t_out_ptr: *mut f32,
) {
    if ck_ptr.is_null() || mi_ptr.is_null() || t_out_ptr.is_null() {
        return;
    }

    let ck_slice = std::slice::from_raw_parts(ck_ptr, 16);
    let mi_slice = std::slice::from_raw_parts(mi_ptr, 16);
    let t_slice = std::slice::from_raw_parts_mut(t_out_ptr, 16);

    let mut ck_m = [[0.0f32; 4]; 4];
    let mut mi_m = [[0.0f32; 4]; 4];
    let mut t_m = [[0.0f32; 4]; 4];

    for r in 0..4 {
        for c in 0..4 {
            ck_m[r][c] = ck_slice[r * 4 + c];
            mi_m[r][c] = mi_slice[r * 4 + c];
            t_m[r][c] = t_slice[r * 4 + c];
        }
    }

    apply_road_plane_to_projector(&ck_m, &mi_m, &mut t_m);

    for r in 0..4 {
        for c in 0..4 {
            t_slice[r * 4 + c] = t_m[r][c];
        }
    }
}

#[no_mangle]
pub extern "C" fn nfs_headlight_compute_road_attenuation(pz: f32, pw: f32) -> f32 {
    compute_headlight_attenuation(pz, pw)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_road_plane_normal() {
        let up = [0.0, 0.0, 1.0];
        let pos = [10.0, 20.0, 5.0];
        let g = compute_road_plane_normal_dist(up, pos);
        assert_eq!(g, [0.0, 0.0, 1.0, -5.0]);
    }

    #[test]
    fn test_headlight_attenuation_level_ground() {
        // Car on ground: pz = 0.0, pw = 10.0 -> road factor should be saturated at 1.0
        let atten = compute_headlight_attenuation(0.0, 10.0);
        assert_eq!(atten, 1.0);
    }

    #[test]
    fn test_headlight_attenuation_overpass_bridge() {
        // Car on overpass bridge 8 meters above street: pz = -8.0, pw = 10.0
        // ( -8.0 + 3.5 + 0.6 ) / 1.5 = -3.9 / 1.5 < 0 -> atten should be 0.0 (fully culled)
        let atten = compute_headlight_attenuation(-8.0, 10.0);
        assert_eq!(atten, 0.0);
    }

    #[test]
    fn test_headlight_attenuation_dip_transition() {
        // Road dipping: pz = -2.5, pw = 5.0
        // ( -2.5 + 3.5 + 0.3 ) / 1.5 = 1.3 / 1.5 = 0.866...
        let atten = compute_headlight_attenuation(-2.5, 5.0);
        assert!(atten > 0.8 && atten < 0.9);
    }
}
