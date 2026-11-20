//! Dynamic Camera Forward-Only Back-Culling Engine.
//!
//! Evaluates 3D world geometry and draw calls against the active player camera.
//! Automatically discards draw calls whose geometry lies in the backward half-space
//! behind the player's camera plane or beyond the active draw distance horizon.
//!
//! This eliminates GPU vertex shading, triangle setup, rasterization, and overdraw
//! for non-visible objects behind the player.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

pub const DEFAULT_CULL_MARGIN: f32 = 6.0; // 6.0m margin for object bounding radius
pub const DEFAULT_BACK_CULL_DISTANCE: f32 = 50.0; // 50.0m back-cull distance behind camera

pub struct CameraCullEngine {
    enabled: AtomicBool,
    cull_margin: AtomicU32,       // f32 bits
    far_distance: AtomicU32,      // f32 bits
    back_cull_dist: AtomicU32,    // f32 bits (50m default)
    // Camera world position & forward direction vector
    cam_pos: [AtomicU32; 3],      // (x, y, z) f32 bits
    cam_fwd: [AtomicU32; 3],      // (fx, fy, fz) f32 bits
    cam_valid: AtomicBool,
    // D3DTS_VIEW columns in eye space:
    // col 0: (V00, V10, V20, V30) - eye X (right)
    v00: AtomicU32,
    v10: AtomicU32,
    v20: AtomicU32,
    v30: AtomicU32,
    // col 1: (V01, V11, V21, V31) - eye Y (up)
    v01: AtomicU32,
    v11: AtomicU32,
    v21: AtomicU32,
    v31: AtomicU32,
    // col 2: (V02, V12, V22, V32) - eye Z (forward)
    v02: AtomicU32,
    v12: AtomicU32,
    v22: AtomicU32,
    v32: AtomicU32,
    view_valid: AtomicBool,
    // Projection scale factors (P00, P11) for FOV / lateral test
    p00: AtomicU32,
    p11: AtomicU32,
    proj_valid: AtomicBool,
    // Current D3DTS_WORLD translation
    world_pos: [AtomicU32; 3],    // (x, y, z) f32 bits
    world_is_identity: AtomicBool,
    world_valid: AtomicBool,
    // State flags
    is_ortho: AtomicBool,
    is_screen_space: AtomicBool,
    is_top_down: AtomicBool,
    is_sub_viewport: AtomicBool,
    bypass: AtomicBool,
    // Metrics
    total_draws: AtomicU64,
    culled_draws: AtomicU64,
}

impl CameraCullEngine {
    pub const fn new() -> Self {
        Self {
            enabled: AtomicBool::new(true),
            cull_margin: AtomicU32::new(DEFAULT_CULL_MARGIN.to_bits()),
            far_distance: AtomicU32::new(0), // 0 = unlimited
            back_cull_dist: AtomicU32::new(DEFAULT_BACK_CULL_DISTANCE.to_bits()),
            cam_pos: [AtomicU32::new(0), AtomicU32::new(0), AtomicU32::new(0)],
            cam_fwd: [AtomicU32::new(0), AtomicU32::new(0), AtomicU32::new(0)],
            cam_valid: AtomicBool::new(false),
            v00: AtomicU32::new(0),
            v10: AtomicU32::new(0),
            v20: AtomicU32::new(0),
            v30: AtomicU32::new(0),
            v01: AtomicU32::new(0),
            v11: AtomicU32::new(0),
            v21: AtomicU32::new(0),
            v31: AtomicU32::new(0),
            v02: AtomicU32::new(0),
            v12: AtomicU32::new(0),
            v22: AtomicU32::new(0),
            v32: AtomicU32::new(0),
            view_valid: AtomicBool::new(false),
            p00: AtomicU32::new(0),
            p11: AtomicU32::new(0),
            proj_valid: AtomicBool::new(false),
            world_pos: [AtomicU32::new(0), AtomicU32::new(0), AtomicU32::new(0)],
            world_is_identity: AtomicBool::new(true),
            world_valid: AtomicBool::new(false),
            is_ortho: AtomicBool::new(false),
            is_screen_space: AtomicBool::new(false),
            is_top_down: AtomicBool::new(false),
            is_sub_viewport: AtomicBool::new(false),
            bypass: AtomicBool::new(false),
            total_draws: AtomicU64::new(0),
            culled_draws: AtomicU64::new(0),
        }
    }

    #[inline]
    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Acquire)
    }

    #[inline]
    pub fn set_enabled(&self, val: bool) {
        self.enabled.store(val, Ordering::Release);
    }

    #[inline]
    pub fn set_margin(&self, margin: f32) {
        self.cull_margin.store(margin.to_bits(), Ordering::Relaxed);
    }

    #[inline]
    pub fn get_margin(&self) -> f32 {
        f32::from_bits(self.cull_margin.load(Ordering::Relaxed))
    }

    #[inline]
    pub fn set_far_distance(&self, dist: f32) {
        self.far_distance.store(dist.to_bits(), Ordering::Relaxed);
    }

    #[inline]
    pub fn get_far_distance(&self) -> f32 {
        f32::from_bits(self.far_distance.load(Ordering::Relaxed))
    }

    #[inline]
    pub fn set_back_cull_distance(&self, dist: f32) {
        self.back_cull_dist.store(dist.to_bits(), Ordering::Relaxed);
    }

    #[inline]
    pub fn get_back_cull_distance(&self) -> f32 {
        f32::from_bits(self.back_cull_dist.load(Ordering::Relaxed))
    }

    /// Updates the player camera position and forward direction vector from guest memory.
    pub fn update_camera(&self, pos: [f32; 3], fwd: [f32; 3]) {
        self.cam_pos[0].store(pos[0].to_bits(), Ordering::Relaxed);
        self.cam_pos[1].store(pos[1].to_bits(), Ordering::Relaxed);
        self.cam_pos[2].store(pos[2].to_bits(), Ordering::Relaxed);

        self.cam_fwd[0].store(fwd[0].to_bits(), Ordering::Relaxed);
        self.cam_fwd[1].store(fwd[1].to_bits(), Ordering::Relaxed);
        self.cam_fwd[2].store(fwd[2].to_bits(), Ordering::Relaxed);

        self.cam_valid.store(true, Ordering::Release);
    }

    /// Updates D3DTS_VIEW matrix columns (eye space axes and translation).
    pub fn update_view_matrix(&self, m: &[f32; 16]) {
        // Col 0: Right vector (X in eye space)
        self.v00.store(m[0].to_bits(), Ordering::Relaxed);
        self.v10.store(m[4].to_bits(), Ordering::Relaxed);
        self.v20.store(m[8].to_bits(), Ordering::Relaxed);
        self.v30.store(m[12].to_bits(), Ordering::Relaxed);

        // Col 1: Up vector (Y in eye space)
        self.v01.store(m[1].to_bits(), Ordering::Relaxed);
        self.v11.store(m[5].to_bits(), Ordering::Relaxed);
        self.v21.store(m[9].to_bits(), Ordering::Relaxed);
        self.v31.store(m[13].to_bits(), Ordering::Relaxed);

        // Col 2: Forward vector (Z in eye space)
        self.v02.store(m[2].to_bits(), Ordering::Relaxed);
        self.v12.store(m[6].to_bits(), Ordering::Relaxed);
        self.v22.store(m[10].to_bits(), Ordering::Relaxed);
        self.v32.store(m[14].to_bits(), Ordering::Relaxed);
        self.view_valid.store(true, Ordering::Release);

        // Detect top-down / overhead camera (e.g. minimap radar pass or fullscreen world map):
        // In D3D row-major matrices, eye-space forward vector in world coordinates is (m[2], m[6], m[10]).
        // Looking down along world vertical (Z-up or Y-up) results in:
        // 1. Steep vertical component (|m[10]| > 0.35 if Z-up, or |m[6]| > 0.35 if Y-up), or
        // 2. Small horizontal component (m[2]^2 + m[6]^2 < 0.60 if Z-up, or m[2]^2 + m[10]^2 < 0.60 if Y-up).
        let z_steep = m[10].abs() > 0.35;
        let y_steep = m[6].abs() > 0.35;
        let horiz_z_sq = m[2] * m[2] + m[6] * m[6];
        let horiz_y_sq = m[2] * m[2] + m[10] * m[10];
        let top_down = z_steep || y_steep || (horiz_z_sq < 0.60) || (horiz_y_sq < 0.60);
        self.is_top_down.store(top_down, Ordering::Release);
    }

    /// Updates D3DTS_PROJECTION matrix, detecting orthographic vs perspective projections.
    pub fn update_proj_matrix(&self, m: &[f32; 16]) {
        // In DirectX projection matrix:
        // Perspective has m[11] == 1.0 (or -1.0) and m[15] == 0.0.
        // Orthographic has m[11] == 0.0 and m[15] != 0.0.
        let ortho = m[11].abs() < 1e-3;
        self.is_ortho.store(ortho, Ordering::Release);

        if !ortho && m[0].abs() > 1e-4 && m[5].abs() > 1e-4 {
            self.p00.store(m[0].to_bits(), Ordering::Relaxed);
            self.p11.store(m[5].to_bits(), Ordering::Relaxed);
            self.proj_valid.store(true, Ordering::Release);
        } else {
            self.proj_valid.store(false, Ordering::Release);
        }
    }

    /// Updates D3DTS_WORLD matrix translation components.
    pub fn update_world_matrix(&self, m: &[f32; 16]) {
        let is_id = (m[12].abs() < 1e-4)
            && (m[13].abs() < 1e-4)
            && (m[14].abs() < 1e-4)
            && ((m[0] - 1.0).abs() < 1e-3)
            && ((m[5] - 1.0).abs() < 1e-3)
            && ((m[10] - 1.0).abs() < 1e-3);

        self.world_is_identity.store(is_id, Ordering::Relaxed);
        self.world_pos[0].store(m[12].to_bits(), Ordering::Relaxed);
        self.world_pos[1].store(m[13].to_bits(), Ordering::Relaxed);
        self.world_pos[2].store(m[14].to_bits(), Ordering::Relaxed);
        self.world_valid.store(true, Ordering::Release);
    }

    /// Notifies whether the active FVF is 2D screen-space transformed (D3DFVF_XYZRHW).
    pub fn set_screen_space(&self, is_screen: bool) {
        self.is_screen_space.store(is_screen, Ordering::Release);
    }

    /// Sets whether the active rendering viewport is a sub-viewport (minimap radar, rearview mirror).
    #[inline]
    pub fn set_sub_viewport(&self, is_sub: bool) {
        self.is_sub_viewport.store(is_sub, Ordering::Release);
    }

    #[inline]
    pub fn is_sub_viewport(&self) -> bool {
        self.is_sub_viewport.load(Ordering::Relaxed)
    }

    /// Sets explicit cull bypass flag (e.g. during minimap radar rendering pass).
    #[inline]
    pub fn set_bypass(&self, bypass: bool) {
        self.bypass.store(bypass, Ordering::Release);
    }

    #[inline]
    pub fn is_bypassed(&self) -> bool {
        self.bypass.load(Ordering::Relaxed)
    }

    /// Tests whether the impending draw call should be culled.
    ///
    /// Evaluates geometry dynamically relative to the active camera:
    /// 1. Behind camera: culled if further than back_cull_distance (50m default).
    ///    When the player looks back, the camera direction dynamically flips:
    ///    the car's rear becomes in front (drawn up to draw distance), and the front
    ///    becomes behind (culled beyond 50m).
    /// 2. In front of camera: culled if beyond selected draw distance preset (far_distance).
    /// 3. Off-screen frustum: culled if outside lateral field of view (not displayed).
    #[inline]
    pub fn test_draw(&self) -> bool {
        self.total_draws.fetch_add(1, Ordering::Relaxed);

        if !self.is_enabled() || self.bypass.load(Ordering::Relaxed) {
            return false;
        }

        // Never cull 2D screen-space primitives (UI, HUD, minimap, speedometer),
        // orthographic / top-down cameras (minimap radar, world map),
        // or sub-viewport rendering passes (radar minimap PIP, rearview mirror)
        if self.is_screen_space.load(Ordering::Relaxed)
            || self.is_ortho.load(Ordering::Relaxed)
            || self.is_top_down.load(Ordering::Relaxed)
            || self.is_sub_viewport.load(Ordering::Relaxed)
        {
            return false;
        }

        // If World matrix is Identity, this is fullscreen/postprocess or raw scenery
        if self.world_is_identity.load(Ordering::Relaxed) || !self.world_valid.load(Ordering::Relaxed) {
            return false;
        }

        let margin = self.get_margin();
        let far_dist = self.get_far_distance();
        let back_dist = self.get_back_cull_distance();

        let wx = f32::from_bits(self.world_pos[0].load(Ordering::Relaxed));
        let wy = f32::from_bits(self.world_pos[1].load(Ordering::Relaxed));
        let wz = f32::from_bits(self.world_pos[2].load(Ordering::Relaxed));

        // Evaluate eye-space coordinates relative to active camera
        let (fwd_distance, eye_x, eye_y, has_eye_xy) = if self.view_valid.load(Ordering::Relaxed) {
            let v02 = f32::from_bits(self.v02.load(Ordering::Relaxed));
            let v12 = f32::from_bits(self.v12.load(Ordering::Relaxed));
            let v22 = f32::from_bits(self.v22.load(Ordering::Relaxed));
            let v32 = f32::from_bits(self.v32.load(Ordering::Relaxed));
            let ez = wx * v02 + wy * v12 + wz * v22 + v32;

            let v00 = f32::from_bits(self.v00.load(Ordering::Relaxed));
            let v10 = f32::from_bits(self.v10.load(Ordering::Relaxed));
            let v20 = f32::from_bits(self.v20.load(Ordering::Relaxed));
            let v30 = f32::from_bits(self.v30.load(Ordering::Relaxed));
            let ex = wx * v00 + wy * v10 + wz * v20 + v30;

            let v01 = f32::from_bits(self.v01.load(Ordering::Relaxed));
            let v11 = f32::from_bits(self.v11.load(Ordering::Relaxed));
            let v21 = f32::from_bits(self.v21.load(Ordering::Relaxed));
            let v31 = f32::from_bits(self.v31.load(Ordering::Relaxed));
            let ey = wx * v01 + wy * v11 + wz * v21 + v31;

            (ez, ex, ey, true)
        } else if self.cam_valid.load(Ordering::Relaxed) {
            let cx = f32::from_bits(self.cam_pos[0].load(Ordering::Relaxed));
            let cy = f32::from_bits(self.cam_pos[1].load(Ordering::Relaxed));
            let cz = f32::from_bits(self.cam_pos[2].load(Ordering::Relaxed));

            let fx = f32::from_bits(self.cam_fwd[0].load(Ordering::Relaxed));
            let fy = f32::from_bits(self.cam_fwd[1].load(Ordering::Relaxed));
            let fz = f32::from_bits(self.cam_fwd[2].load(Ordering::Relaxed));

            let ez = (wx - cx) * fx + (wy - cy) * fy + (wz - cz) * fz;
            (ez, 0.0, 0.0, false)
        } else {
            return false;
        };

        // 1. Dynamic Back-Culling Check:
        // Object is behind the active camera viewing vector by more than back_dist (50M).
        // Preserves near-field rear geometry (-50m to 0m) for trailing cars, shadows, and smoke.
        if fwd_distance < -back_dist {
            self.culled_draws.fetch_add(1, Ordering::Relaxed);
            return true;
        }

        // 2. Far Horizon Check:
        // Object is in front of camera beyond the user's selected draw distance preset.
        if far_dist > 0.0 && fwd_distance > (far_dist + margin) {
            self.culled_draws.fetch_add(1, Ordering::Relaxed);
            return true;
        }

        // 3. Lateral / Frustum Check:
        // Object is in front of camera but outside the display view angle (not displayed on screen).
        if has_eye_xy && self.proj_valid.load(Ordering::Relaxed) && fwd_distance > margin {
            let p00 = f32::from_bits(self.p00.load(Ordering::Relaxed));
            let p11 = f32::from_bits(self.p11.load(Ordering::Relaxed));
            if p00 > 0.01 && p11 > 0.01 {
                let limit_x = (fwd_distance / p00) * 1.45 + (margin * 2.5);
                let limit_y = (fwd_distance / p11) * 1.45 + (margin * 2.5);
                if eye_x.abs() > limit_x || eye_y.abs() > limit_y {
                    self.culled_draws.fetch_add(1, Ordering::Relaxed);
                    return true;
                }
            }
        }

        false
    }

    pub fn stats(&self, out_total: *mut u64, out_culled: *mut u64) {
        if !out_total.is_null() {
            unsafe { *out_total = self.total_draws.load(Ordering::Relaxed) };
        }
        if !out_culled.is_null() {
            unsafe { *out_culled = self.culled_draws.load(Ordering::Relaxed) };
        }
    }
}

pub static GLOBAL_CAMERA_CULL: CameraCullEngine = CameraCullEngine::new();

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_enabled(enabled: i32) {
    GLOBAL_CAMERA_CULL.set_enabled(enabled != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_is_enabled() -> i32 {
    if GLOBAL_CAMERA_CULL.is_enabled() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_back_distance(dist: f32) {
    GLOBAL_CAMERA_CULL.set_back_cull_distance(dist);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_get_back_distance() -> f32 {
    GLOBAL_CAMERA_CULL.get_back_cull_distance()
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_margin(margin: f32) {
    GLOBAL_CAMERA_CULL.set_margin(margin);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_far_distance(dist: f32) {
    GLOBAL_CAMERA_CULL.set_far_distance(dist);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_get_far_distance() -> f32 {
    GLOBAL_CAMERA_CULL.get_far_distance()
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_update_camera(
    pos_x: f32, pos_y: f32, pos_z: f32,
    fwd_x: f32, fwd_y: f32, fwd_z: f32,
) {
    GLOBAL_CAMERA_CULL.update_camera([pos_x, pos_y, pos_z], [fwd_x, fwd_y, fwd_z]);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_update_view(view_ptr: *const f32) {
    if !view_ptr.is_null() {
        let slice = std::slice::from_raw_parts(view_ptr, 16);
        let mut m = [0.0f32; 16];
        m.copy_from_slice(slice);
        GLOBAL_CAMERA_CULL.update_view_matrix(&m);
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_update_proj(proj_ptr: *const f32) {
    if !proj_ptr.is_null() {
        let slice = std::slice::from_raw_parts(proj_ptr, 16);
        let mut m = [0.0f32; 16];
        m.copy_from_slice(slice);
        GLOBAL_CAMERA_CULL.update_proj_matrix(&m);
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_update_world(world_ptr: *const f32) {
    if !world_ptr.is_null() {
        let slice = std::slice::from_raw_parts(world_ptr, 16);
        let mut m = [0.0f32; 16];
        m.copy_from_slice(slice);
        GLOBAL_CAMERA_CULL.update_world_matrix(&m);
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_screen_space(is_screen_space: i32) {
    GLOBAL_CAMERA_CULL.set_screen_space(is_screen_space != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_sub_viewport(is_sub: i32) {
    GLOBAL_CAMERA_CULL.set_sub_viewport(is_sub != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_is_top_down() -> i32 {
    if GLOBAL_CAMERA_CULL.is_top_down.load(Ordering::Relaxed) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_is_ortho() -> i32 {
    if GLOBAL_CAMERA_CULL.is_ortho.load(Ordering::Relaxed) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_is_sub_viewport() -> i32 {
    if GLOBAL_CAMERA_CULL.is_sub_viewport() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_bypass(bypass: i32) {
    GLOBAL_CAMERA_CULL.set_bypass(bypass != 0);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_is_bypassed() -> i32 {
    if GLOBAL_CAMERA_CULL.is_bypassed() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_test_draw() -> i32 {
    if GLOBAL_CAMERA_CULL.test_draw() { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_stats(out_total: *mut u64, out_culled: *mut u64) {
    GLOBAL_CAMERA_CULL.stats(out_total, out_culled);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_get_camera(out_pos: *mut f32, out_fwd: *mut f32) -> i32 {
    if !GLOBAL_CAMERA_CULL.cam_valid.load(Ordering::Relaxed) {
        return 0;
    }
    if !out_pos.is_null() {
        *out_pos = f32::from_bits(GLOBAL_CAMERA_CULL.cam_pos[0].load(Ordering::Relaxed));
        *out_pos.add(1) = f32::from_bits(GLOBAL_CAMERA_CULL.cam_pos[1].load(Ordering::Relaxed));
        *out_pos.add(2) = f32::from_bits(GLOBAL_CAMERA_CULL.cam_pos[2].load(Ordering::Relaxed));
    }
    if !out_fwd.is_null() {
        *out_fwd = f32::from_bits(GLOBAL_CAMERA_CULL.cam_fwd[0].load(Ordering::Relaxed));
        *out_fwd.add(1) = f32::from_bits(GLOBAL_CAMERA_CULL.cam_fwd[1].load(Ordering::Relaxed));
        *out_fwd.add(2) = f32::from_bits(GLOBAL_CAMERA_CULL.cam_fwd[2].load(Ordering::Relaxed));
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_camera_culling_front_and_back_50m() {
        let engine = CameraCullEngine::new();
        engine.set_enabled(true);
        engine.set_margin(6.0);
        engine.set_back_cull_distance(50.0);

        // Camera at origin, pointing along +Z
        engine.update_camera([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]);

        // 1. Object in front: Z = +20.0m -> Should NOT cull
        let mut world_front = [0.0f32; 16];
        world_front[0] = 1.0; world_front[5] = 1.0; world_front[10] = 1.0; world_front[15] = 1.0;
        world_front[12] = 0.0; world_front[13] = 0.0; world_front[14] = 20.0;
        engine.update_world_matrix(&world_front);
        assert!(!engine.test_draw());

        // 2. Object trailing behind camera within 50m: Z = -30.0m -> Should NOT cull (near field kept)
        let mut world_near_back = [0.0f32; 16];
        world_near_back[0] = 1.0; world_near_back[5] = 1.0; world_near_back[10] = 1.0; world_near_back[15] = 1.0;
        world_near_back[12] = 0.0; world_near_back[13] = 0.0; world_near_back[14] = -30.0;
        engine.update_world_matrix(&world_near_back);
        assert!(!engine.test_draw());

        // 3. Object further behind than 50m: Z = -60.0m (< -50m) -> Should CULL
        let mut world_far_back = [0.0f32; 16];
        world_far_back[0] = 1.0; world_far_back[5] = 1.0; world_far_back[10] = 1.0; world_far_back[15] = 1.0;
        world_far_back[12] = 0.0; world_far_back[13] = 0.0; world_far_back[14] = -60.0;
        engine.update_world_matrix(&world_far_back);
        assert!(engine.test_draw());
    }

    #[test]
    fn test_dynamic_look_back_culling() {
        let engine = CameraCullEngine::new();
        engine.set_enabled(true);
        engine.set_margin(6.0);
        engine.set_back_cull_distance(50.0);
        engine.set_far_distance(175.0); // 175m draw distance

        // Player camera looking backwards along -Z
        // Standard D3D view matrix looking backwards:
        let mut view_look_back = [0.0f32; 16];
        view_look_back[0] = -1.0; // X flipped
        view_look_back[5] = 1.0;
        view_look_back[10] = -1.0; // Z points along -Z (behind car)
        view_look_back[15] = 1.0;
        engine.update_view_matrix(&view_look_back);

        // 1. Object behind car: Z = -60.0m
        // In view space: ez = 0*0 + 0*0 + (-60.0)*(-1.0) = +60.0m (IN FRONT of backwards camera!)
        // Should NOT cull, it's displayed on screen!
        let mut world_behind_car = [0.0f32; 16];
        world_behind_car[0] = 1.0; world_behind_car[5] = 1.0; world_behind_car[10] = 1.0; world_behind_car[15] = 1.0;
        world_behind_car[14] = -60.0;
        engine.update_world_matrix(&world_behind_car);
        assert!(!engine.test_draw());

        // 2. Object in front of car: Z = +60.0m
        // In view space: ez = 0*0 + 0*0 + (60.0)*(-1.0) = -60.0m (BEHIND backwards camera, > 50m!)
        // Should CULL dynamically!
        let mut world_front_of_car = [0.0f32; 16];
        world_front_of_car[0] = 1.0; world_front_of_car[5] = 1.0; world_front_of_car[10] = 1.0; world_front_of_car[15] = 1.0;
        world_front_of_car[14] = 60.0;
        engine.update_world_matrix(&world_front_of_car);
        assert!(engine.test_draw());
    }

    #[test]
    fn test_draw_distance_horizon_culling() {
        let engine = CameraCullEngine::new();
        engine.set_enabled(true);
        engine.set_margin(4.0);
        engine.set_far_distance(100.0); // 100m draw distance (Performance preset)

        engine.update_camera([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]);

        // Object within 100m: Z = 80.0m -> Should NOT cull
        let mut world_mid = [0.0f32; 16];
        world_mid[0] = 1.0; world_mid[5] = 1.0; world_mid[10] = 1.0; world_mid[15] = 1.0;
        world_mid[14] = 80.0;
        engine.update_world_matrix(&world_mid);
        assert!(!engine.test_draw());

        // Object beyond 100m + margin: Z = 120.0m -> Should CULL
        let mut world_far = [0.0f32; 16];
        world_far[0] = 1.0; world_far[5] = 1.0; world_far[10] = 1.0; world_far[15] = 1.0;
        world_far[14] = 120.0;
        engine.update_world_matrix(&world_far);
        assert!(engine.test_draw());
    }

    #[test]
    fn test_identity_and_ortho_preservation() {
        let engine = CameraCullEngine::new();
        engine.set_enabled(true);
        engine.update_camera([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]);

        // Identity world matrix (track / terrain batch) -> Never culled
        let mut id_m = [0.0f32; 16];
        id_m[0] = 1.0; id_m[5] = 1.0; id_m[10] = 1.0; id_m[15] = 1.0;
        engine.update_world_matrix(&id_m);
        assert!(!engine.test_draw());

        // Orthographic projection -> Never culled
        let mut ortho_proj = [0.0f32; 16];
        ortho_proj[0] = 1.0; ortho_proj[5] = 1.0; ortho_proj[10] = 1.0; ortho_proj[15] = 1.0; // m[11] = 0, m[15] = 1
        engine.update_proj_matrix(&ortho_proj);
        let mut world_back = [0.0f32; 16];
        world_back[0] = 1.0; world_back[5] = 1.0; world_back[10] = 1.0; world_back[15] = 1.0;
        world_back[14] = -50.0;
        engine.update_world_matrix(&world_back);
        assert!(!engine.test_draw());
    }

    #[test]
    fn test_top_down_and_bypass() {
        let engine = CameraCullEngine::new();
        engine.set_enabled(true);
        engine.update_camera([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]);

        // Object behind camera: Z = -50.0m
        let mut world_back = [0.0f32; 16];
        world_back[0] = 1.0; world_back[5] = 1.0; world_back[10] = 1.0; world_back[15] = 1.0;
        world_back[14] = -50.0;
        engine.update_world_matrix(&world_back);
        assert!(engine.test_draw());

        // 1. Explicit bypass enables radar pass to preserve all draw calls
        engine.set_bypass(true);
        assert!(!engine.test_draw());
        engine.set_bypass(false);
        assert!(engine.test_draw());

        // 2. Top-down view matrix (looking straight down along -Y or -Z)
        let mut top_down_view = [0.0f32; 16];
        top_down_view[0] = 1.0;
        top_down_view[5] = 0.0;
        top_down_view[10] = 1.0; // looking down
        top_down_view[15] = 1.0;
        engine.update_view_matrix(&top_down_view);
        assert!(!engine.test_draw());

        // 3. Sub-viewport radar minimap pass (preserves all draw calls regardless of camera)
        let engine_sub = CameraCullEngine::new();
        engine_sub.set_enabled(true);
        engine_sub.update_camera([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]);
        engine_sub.update_world_matrix(&world_back);
        assert!(engine_sub.test_draw()); // Culled when not sub-viewport
        engine_sub.set_sub_viewport(true);
        assert!(!engine_sub.test_draw()); // Not culled in sub-viewport!
    }
}
