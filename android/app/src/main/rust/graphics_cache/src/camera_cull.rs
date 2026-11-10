//! Dynamic Camera Forward-Only Back-Culling Engine.
//!
//! Evaluates 3D world geometry and draw calls against the active player camera.
//! Automatically discards draw calls whose geometry lies in the backward half-space
//! behind the player's camera plane or beyond the active draw distance horizon.
//!
//! This eliminates GPU vertex shading, triangle setup, rasterization, and overdraw
//! for non-visible objects behind the player.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

pub const DEFAULT_CULL_MARGIN: f32 = 4.0; // 4.0m margin for object bounding radius

pub struct CameraCullEngine {
    enabled: AtomicBool,
    cull_margin: AtomicU32,   // f32 bits
    far_distance: AtomicU32,  // f32 bits
    // Camera world position & forward direction vector
    cam_pos: [AtomicU32; 3],  // (x, y, z) f32 bits
    cam_fwd: [AtomicU32; 3],  // (fx, fy, fz) f32 bits
    cam_valid: AtomicBool,
    // D3DTS_VIEW column 2: (V02, V12, V22, V32) for eye-space depth evaluation
    v02: AtomicU32,
    v12: AtomicU32,
    v22: AtomicU32,
    v32: AtomicU32,
    view_valid: AtomicBool,
    // Current D3DTS_WORLD translation
    world_pos: [AtomicU32; 3], // (x, y, z) f32 bits
    world_is_identity: AtomicBool,
    world_valid: AtomicBool,
    // State flags
    is_ortho: AtomicBool,
    is_screen_space: AtomicBool,
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
            cam_pos: [AtomicU32::new(0), AtomicU32::new(0), AtomicU32::new(0)],
            cam_fwd: [AtomicU32::new(0), AtomicU32::new(0), AtomicU32::new(0)],
            cam_valid: AtomicBool::new(false),
            v02: AtomicU32::new(0),
            v12: AtomicU32::new(0),
            v22: AtomicU32::new(0),
            v32: AtomicU32::new(0),
            view_valid: AtomicBool::new(false),
            world_pos: [AtomicU32::new(0), AtomicU32::new(0), AtomicU32::new(0)],
            world_is_identity: AtomicBool::new(true),
            world_valid: AtomicBool::new(false),
            is_ortho: AtomicBool::new(false),
            is_screen_space: AtomicBool::new(false),
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

    /// Updates D3DTS_VIEW matrix column 2 (Z axis in camera space).
    pub fn update_view_matrix(&self, m: &[f32; 16]) {
        self.v02.store(m[2].to_bits(), Ordering::Relaxed);
        self.v12.store(m[6].to_bits(), Ordering::Relaxed);
        self.v22.store(m[10].to_bits(), Ordering::Relaxed);
        self.v32.store(m[14].to_bits(), Ordering::Relaxed);
        self.view_valid.store(true, Ordering::Release);
    }

    /// Updates D3DTS_PROJECTION matrix, detecting orthographic vs perspective projections.
    pub fn update_proj_matrix(&self, m: &[f32; 16]) {
        // In DirectX projection matrix:
        // Perspective has m[11] == 1.0 (or -1.0) and m[15] == 0.0.
        // Orthographic has m[11] == 0.0 and m[15] == 1.0.
        let ortho = (m[11].abs() < 1e-4) && ((m[15] - 1.0).abs() < 1e-3);
        self.is_ortho.store(ortho, Ordering::Release);
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

    /// Tests whether the impending draw call should be culled.
    ///
    /// Returns `true` if the draw call is strictly behind the camera or out of range.
    #[inline]
    pub fn test_draw(&self) -> bool {
        self.total_draws.fetch_add(1, Ordering::Relaxed);

        if !self.is_enabled() {
            return false;
        }

        // Never cull 2D screen-space primitives (UI, HUD, minimap, speedometer)
        if self.is_screen_space.load(Ordering::Relaxed) || self.is_ortho.load(Ordering::Relaxed) {
            return false;
        }

        // If World matrix is Identity, this is fullscreen/postprocess or raw scenery
        if self.world_is_identity.load(Ordering::Relaxed) || !self.world_valid.load(Ordering::Relaxed) {
            return false;
        }

        let margin = self.get_margin();
        let far_dist = self.get_far_distance();

        let wx = f32::from_bits(self.world_pos[0].load(Ordering::Relaxed));
        let wy = f32::from_bits(self.world_pos[1].load(Ordering::Relaxed));
        let wz = f32::from_bits(self.world_pos[2].load(Ordering::Relaxed));

        // Evaluate distance along camera viewing vector
        let fwd_distance = if self.cam_valid.load(Ordering::Relaxed) {
            let cx = f32::from_bits(self.cam_pos[0].load(Ordering::Relaxed));
            let cy = f32::from_bits(self.cam_pos[1].load(Ordering::Relaxed));
            let cz = f32::from_bits(self.cam_pos[2].load(Ordering::Relaxed));

            let fx = f32::from_bits(self.cam_fwd[0].load(Ordering::Relaxed));
            let fy = f32::from_bits(self.cam_fwd[1].load(Ordering::Relaxed));
            let fz = f32::from_bits(self.cam_fwd[2].load(Ordering::Relaxed));

            (wx - cx) * fx + (wy - cy) * fy + (wz - cz) * fz
        } else if self.view_valid.load(Ordering::Relaxed) {
            let v02 = f32::from_bits(self.v02.load(Ordering::Relaxed));
            let v12 = f32::from_bits(self.v12.load(Ordering::Relaxed));
            let v22 = f32::from_bits(self.v22.load(Ordering::Relaxed));
            let v32 = f32::from_bits(self.v32.load(Ordering::Relaxed));

            wx * v02 + wy * v12 + wz * v22 + v32
        } else {
            return false;
        };

        // 1. Back-Cull Check: Object is strictly behind player camera plane
        if fwd_distance < -margin {
            self.culled_draws.fetch_add(1, Ordering::Relaxed);
            return true;
        }

        // 2. Far Horizon Check: Object is beyond the configured draw distance
        if far_dist > 0.0 && fwd_distance > (far_dist + margin) {
            self.culled_draws.fetch_add(1, Ordering::Relaxed);
            return true;
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
pub unsafe extern "C" fn nfs_camera_cull_set_margin(margin: f32) {
    GLOBAL_CAMERA_CULL.set_margin(margin);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_cull_set_far_distance(dist: f32) {
    GLOBAL_CAMERA_CULL.set_far_distance(dist);
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
    fn test_camera_culling_front_and_back() {
        let engine = CameraCullEngine::new();
        engine.set_enabled(true);
        engine.set_margin(4.0);

        // Camera at origin, pointing along +Z
        engine.update_camera([0.0, 0.0, 0.0], [0.0, 0.0, 1.0]);

        // 1. Object in front: Z = +20.0m -> Should NOT cull
        let mut world_front = [0.0f32; 16];
        world_front[0] = 1.0; world_front[5] = 1.0; world_front[10] = 1.0; world_front[15] = 1.0;
        world_front[12] = 0.0; world_front[13] = 0.0; world_front[14] = 20.0;
        engine.update_world_matrix(&world_front);
        assert!(!engine.test_draw());

        // 2. Object behind: Z = -15.0m (< -4.0m margin) -> Should CULL
        let mut world_back = [0.0f32; 16];
        world_back[0] = 1.0; world_back[5] = 1.0; world_back[10] = 1.0; world_back[15] = 1.0;
        world_back[12] = 0.0; world_back[13] = 0.0; world_back[14] = -15.0;
        engine.update_world_matrix(&world_back);
        assert!(engine.test_draw());

        // 3. Object right at edge of margin: Z = -3.5m (within 4m margin) -> Should NOT cull
        let mut world_margin = [0.0f32; 16];
        world_margin[0] = 1.0; world_margin[5] = 1.0; world_margin[10] = 1.0; world_margin[15] = 1.0;
        world_margin[12] = 0.0; world_margin[13] = 0.0; world_margin[14] = -3.5;
        engine.update_world_matrix(&world_margin);
        assert!(!engine.test_draw());
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
}
