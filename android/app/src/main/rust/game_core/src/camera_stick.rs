//! Right Stick Camera Rotation Engine (PS2 Camera Orbit & Rear-View Controls).
//!
//! Inspired by the PlayStation 2 version of Need for Speed Underground 2 and `nfsu2-web` (`look.c`),
//! where the right analog stick dynamically rotates the chase/drive camera around the car while driving,
//! and snaps directly to the rear when pulled straight back.
//!
//! Technical Architecture:
//! 1. Controller Axis Ingestion (Google Game Controller SDK compatible):
//!    - Samples both standard Android (AXIS_Z / AXIS_RZ) and custom gamepad (AXIS_RX / AXIS_RY) axes.
//!    - Radial deadzone filtering (0.20 threshold) with smooth acceleration curve up to saturation (0.85).
//! 2. Smooth Orbit Dynamics:
//!    - Target yaw: atan2(x, -y) * k (0 is forward, +pi/2 is 90 deg right, -pi/2 is 90 deg left, pi is rear).
//!    - Exponential decay smoothing: yaw += (target_yaw - yaw) * (1.0 - exp(-dt / TAU)).
//!    - Auto-centering: camera smoothly swings back behind car when right stick is released.
//!    - Instant Rear Look: pulling stick straight back snaps yaw to +/-pi.
//! 3. Camera Matrix & Frustum Synchronization:
//!    - Locates active Player 1 car via SPLIT_PLAYERS_INDEX (0x008900ac).
//!    - Locates active camera via G_VIEW1 (0x00832e50 + 0x40) or player mover (0x238 + 0x40).
//!    - Pivot selection: if eye position is within 3.0m of car center (bumper/hood cameras), pivots
//!      about the eye; otherwise orbits around car center.
//!    - Modifies 4x4 row-major view matrix, camera eye, forward vector, and target point for the duration
//!      of the rendered frame, restoring original bytes at EndScene/Present so game simulation is pristine.

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::Instant;

const SPLIT_PLAYERS_INDEX: u32 = 0x008900ac;
const G_VIEW1: u32 = 0x00832e50;
const CAR_POS_OFFSET: u32 = 0x60;
const CAR_VEL_OFFSET: u32 = 0x70;
const CAR_SPEED_OFFSET: u32 = 0x430;
const CAM_POS_OFFSET: u32 = 0x40;
const CAM_FWD_OFFSET: u32 = 0x50;
const CAM_TARGET_OFFSET: u32 = 0x60;
const CAM_SIZE: usize = 0x70;

const G_MOTION_BLUR: u32 = 0x00870cdc;
const G_MOTION_BLUR_WEIGHT: u32 = 0x007ff7b8;
const DEFAULT_BLUR_WEIGHT: f32 = 0.65;

const STICK_DEADZONE: f32 = 0.20;
const STICK_FULL: f32 = 0.85;
const TAU_SECONDS: f32 = 0.09;
const SNAP_REAR_IN: f32 = 0.44;  // within ~25 degrees of straight back
const SNAP_REAR_OUT: f32 = 0.61; // within ~35 degrees
const SNAP_REAR_RADIUS: f32 = 0.50;
const IN_CAR_MAX_METERS: f32 = 3.0;
const REST_YAW_THRESHOLD: f32 = 0.0005;
const PI_F: f32 = std::f32::consts::PI;

pub struct CameraStickEngine {
    enabled: AtomicBool,
    stick_x: AtomicI32, // stored as (f32 * 10000.0) as i32 for lockless atomics
    stick_y: AtomicI32,
    current_yaw: AtomicI32,
    is_snapped_rear: AtomicBool,
    is_applied: AtomicBool,
    last_turn_count: AtomicU64,
    turned_cam_addr: AtomicU32,
    saved_cam_bytes: Mutex<[u8; CAM_SIZE]>,
    last_tick_time: Mutex<Option<Instant>>,
    motion_blur_modified: AtomicBool,
    saved_motion_blur_val: AtomicU32,
    saved_blur_weight: AtomicU32,
}

impl CameraStickEngine {
    pub const fn new() -> Self {
        Self {
            enabled: AtomicBool::new(true),
            stick_x: AtomicI32::new(0),
            stick_y: AtomicI32::new(0),
            current_yaw: AtomicI32::new(0),
            is_snapped_rear: AtomicBool::new(false),
            is_applied: AtomicBool::new(false),
            last_turn_count: AtomicU64::new(0),
            turned_cam_addr: AtomicU32::new(0),
            saved_cam_bytes: Mutex::new([0u8; CAM_SIZE]),
            last_tick_time: Mutex::new(None),
            motion_blur_modified: AtomicBool::new(false),
            saved_motion_blur_val: AtomicU32::new(1),
            saved_blur_weight: AtomicU32::new(0x3f266666), // 0.65f in IEEE 754
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

    /// Sets raw right analog stick axes from controller events.
    pub fn set_stick_input(&self, x: f32, y: f32) {
        let ix = (x * 10000.0) as i32;
        let iy = (y * 10000.0) as i32;
        self.stick_x.store(ix, Ordering::Release);
        self.stick_y.store(iy, Ordering::Release);
    }

    #[inline]
    pub fn get_stick_input(&self) -> (f32, f32) {
        let ix = self.stick_x.load(Ordering::Acquire);
        let iy = self.stick_y.load(Ordering::Acquire);
        (ix as f32 / 10000.0, iy as f32 / 10000.0)
    }

    #[inline]
    pub fn get_yaw(&self) -> f32 {
        let iyaw = self.current_yaw.load(Ordering::Acquire);
        iyaw as f32 / 10000.0
    }

    #[inline]
    fn set_yaw(&self, yaw: f32) {
        let iyaw = (yaw * 10000.0) as i32;
        self.current_yaw.store(iyaw, Ordering::Release);
    }

    /// Calculates target yaw based on right analog stick position.
    pub fn compute_target_yaw(&self, x: f32, y: f32, current_yaw: f32) -> (f32, bool) {
        let r = (x * x + y * y).sqrt();
        if r < STICK_DEADZONE {
            return (0.0, false);
        }

        // Distance from straight down (pi - abs(atan2(x, -y)))
        let from_back = PI_F - x.atan2(-y).abs();
        let was_back = self.is_snapped_rear.load(Ordering::Acquire);
        let is_rear = if was_back {
            from_back < SNAP_REAR_OUT && r > (SNAP_REAR_RADIUS - 0.10)
        } else {
            from_back < SNAP_REAR_IN && r > SNAP_REAR_RADIUS
        };

        if is_rear {
            let snap_side = if current_yaw < -0.05 { -PI_F } else { PI_F };
            return (snap_side, true);
        }

        let k = ((r - STICK_DEADZONE) / (STICK_FULL - STICK_DEADZONE)).clamp(0.0, 1.0);
        let mut dir = x.atan2(-y); // 0 = up, +pi/2 = right, -pi/2 = left
        let side = if current_yaw.abs() > 0.05 {
            if current_yaw > 0.0 { 1.0 } else { -1.0 }
        } else if x >= 0.0 {
            1.0
        } else {
            -1.0
        };

        if dir.abs() > 2.4 && (if dir > 0.0 { 1.0 } else { -1.0 }) != side {
            dir += side * 2.0 * PI_F;
        }

        (dir * k, false)
    }

    /// Advances the smooth camera rotation simulation.
    pub fn update_yaw(&self) -> f32 {
        let now = Instant::now();
        let dt = {
            if let Ok(mut guard) = self.last_tick_time.lock() {
                let prev = guard.replace(now);
                match prev {
                    Some(p) => now.duration_since(p).as_secs_f32().min(0.25),
                    None => 0.016,
                }
            } else {
                0.016
            }
        };

        let (sx, sy) = self.get_stick_input();
        let mut cur_yaw = self.get_yaw();
        let (target_yaw, is_rear) = self.compute_target_yaw(sx, sy, cur_yaw);
        self.is_snapped_rear.store(is_rear, Ordering::Release);

        if is_rear {
            self.set_yaw(target_yaw);
            return target_yaw;
        }

        let mut diff = target_yaw - cur_yaw;
        while diff > PI_F {
            diff -= 2.0 * PI_F;
        }
        while diff < -PI_F {
            diff += 2.0 * PI_F;
        }

        let decay = 1.0 - (-dt / TAU_SECONDS).exp();
        cur_yaw += diff * decay;

        while cur_yaw > PI_F {
            cur_yaw -= 2.0 * PI_F;
        }
        while cur_yaw < -PI_F {
            cur_yaw += 2.0 * PI_F;
        }

        if target_yaw == 0.0 && cur_yaw.abs() < REST_YAW_THRESHOLD {
            cur_yaw = 0.0;
        }

        self.set_yaw(cur_yaw);
        cur_yaw
    }

    /// Rotates a 2D point/vector (p[0], p[1]) about pivot (ox, oy) clockwise by yaw.
    pub fn rotate_xy(p: &mut [f32; 3], is_point: bool, ox: f32, oy: f32, yaw: f32) {
        let (sin_y, cos_y) = yaw.sin_cos();
        let px = if is_point { ox } else { 0.0 };
        let py = if is_point { oy } else { 0.0 };
        let x = p[0] - px;
        let y = p[1] - py;
        p[0] = px + x * cos_y + y * sin_y;
        p[1] = py - x * sin_y + y * cos_y;
    }

    /// Rotates a 4x4 row-major D3D matrix about vertical axis through (ox, oy).
    pub fn rotate_matrix(m: &mut [f32; 16], ox: f32, oy: f32, yaw: f32) {
        let (s, c) = (-yaw).sin_cos();
        let tx = ox - (ox * c + oy * s);
        let ty = oy - (-ox * s + oy * c);

        let mut r0 = [0.0f32; 4];
        let mut r1 = [0.0f32; 4];
        for k in 0..4 {
            r0[k] = c * m[k] - s * m[4 + k];
            r1[k] = s * m[k] + c * m[4 + k];
        }
        for k in 0..4 {
            m[12 + k] += tx * m[k] + ty * m[4 + k];
            m[k] = r0[k];
            m[4 + k] = r1[k];
        }
    }

    /// Prepares camera for the frame by turning it in guest memory.
    pub unsafe fn apply_frame(&self, guest_base: *mut u8) {
        if !self.is_enabled() || guest_base.is_null() {
            return;
        }

        // Prevent double-applying across multiple BeginScene passes of the same frame
        if self.is_applied.load(Ordering::Acquire) {
            return;
        }

        // If roam_android's hook_sub_005c7ec0 is present in libnfsu2_roam.so,
        // camera orbit is fully handled in C during view setup (sub_005c7ec0).
        // Defer completely so Rust does not compete with C view setup.
        static HAS_ROAM: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
        let has_roam = *HAS_ROAM.get_or_init(|| unsafe {
            let sym = libc::dlsym(libc::RTLD_DEFAULT, b"nfs_is_camera_turned_in_guest\0".as_ptr() as *const _);
            !sym.is_null()
        });
        if has_roam {
            return;
        }

        let yaw = self.update_yaw();
        if yaw.abs() < REST_YAW_THRESHOLD || !yaw.is_finite() {
            return;
        }

        // Check player car at SPLIT_PLAYERS_INDEX
        let pl_ptr = guest_base.add(SPLIT_PLAYERS_INDEX as usize) as *const u32;
        let pl = *pl_ptr;
        if pl == 0 || pl >= 0x7fffffff {
            return; // In menus, not in race/world
        }

        let car_ptr = guest_base.add((pl + 4) as usize) as *const u32;
        let car = *car_ptr;
        if car == 0 || car >= 0x7fffffff {
            return;
        }

        // Get car world position
        let car_pos = guest_base.add((car + CAR_POS_OFFSET) as usize) as *const f32;
        let mut ox = *car_pos;
        let mut oy = *car_pos.add(1);
        if !ox.is_finite() || !oy.is_finite() {
            return;
        }

        // Find active drive camera:
        // In SPEED2.EXE, Player + 0x238 is the camera mover. Mover + 0xC is mover type (1 = MOVER_DRIVE).
        // eView 1 is at G_VIEW1 (0x00832e50). eView + 0x40 (0x00832e90) holds the active Camera* pointer.
        let mover_ptr = guest_base.add((pl + 0x238) as usize) as *const u32;
        let mover = *mover_ptr;
        if mover != 0 && mover < 0x7ffff000 {
            let mover_type_ptr = guest_base.add((mover + 0x0c) as usize) as *const u32;
            let mv_type = *mover_type_ptr;
            // Only orbit when driving (type 1). Other types are debug camera (2), cutscene, etc.
            if mv_type != 1 {
                return;
            }
        }

        let view_cam_ptr = guest_base.add((G_VIEW1 + 0x40) as usize) as *const u32;
        let cam = *view_cam_ptr;
        if cam < 0x10000 || cam >= 0x7ffff000 {
            return;
        }

        let cam_raw = guest_base.add(cam as usize);
        let cam_pos = cam_raw.add(CAM_POS_OFFSET as usize) as *mut f32;
        let eye_x = *cam_pos;
        let eye_y = *cam_pos.add(1);
        if !eye_x.is_finite() || !eye_y.is_finite() {
            return;
        }

        // In-car camera detection (bumper/hood view within 3m): pivot at eye
        let dx = eye_x - ox;
        let dy = eye_y - oy;
        if (dx * dx + dy * dy).sqrt() < IN_CAR_MAX_METERS {
            ox = eye_x;
            oy = eye_y;
        }

        // Motion blur suppression is handled dynamically by motion_blur.rs:nfs_motion_blur_should_render
        // during hook_sub_005c5c00 without clobbering G_MOTION_BLUR in guest memory.

        // Validate camera matrix before rotation
        let m_ptr = cam_raw as *mut f32;
        let mut m = [0.0f32; 16];
        std::ptr::copy_nonoverlapping(m_ptr, m.as_mut_ptr(), 16);
        for val in &m {
            if !val.is_finite() {
                return;
            }
        }

        // Save original camera bytes
        if let Ok(mut guard) = self.saved_cam_bytes.lock() {
            std::ptr::copy_nonoverlapping(cam_raw, guard.as_mut_ptr(), CAM_SIZE);
        }
        self.turned_cam_addr.store(cam, Ordering::Release);
        self.is_applied.store(true, Ordering::Release);
        self.last_turn_count.fetch_add(1, Ordering::Relaxed);

        // Rotate matrix
        Self::rotate_matrix(&mut m, ox, oy, yaw);
        for val in &m {
            if !val.is_finite() {
                return;
            }
        }
        std::ptr::copy_nonoverlapping(m.as_ptr(), m_ptr, 16);

        // Rotate eye position
        let mut eye = [*cam_pos, *cam_pos.add(1), *cam_pos.add(2)];
        if eye[0].is_finite() && eye[1].is_finite() && eye[2].is_finite() {
            Self::rotate_xy(&mut eye, true, ox, oy, yaw);
            if eye[0].is_finite() && eye[1].is_finite() && eye[2].is_finite() {
                *cam_pos = eye[0];
                *cam_pos.add(1) = eye[1];
                *cam_pos.add(2) = eye[2];
            }
        }

        // Rotate forward vector
        let fwd_pos = cam_raw.add(CAM_FWD_OFFSET as usize) as *mut f32;
        let mut fwd = [*fwd_pos, *fwd_pos.add(1), *fwd_pos.add(2)];
        if fwd[0].is_finite() && fwd[1].is_finite() && fwd[2].is_finite() {
            Self::rotate_xy(&mut fwd, false, ox, oy, yaw);
            if fwd[0].is_finite() && fwd[1].is_finite() && fwd[2].is_finite() {
                *fwd_pos = fwd[0];
                *fwd_pos.add(1) = fwd[1];
                *fwd_pos.add(2) = fwd[2];
            }
        }

        // Rotate target position
        let tgt_pos = cam_raw.add(CAM_TARGET_OFFSET as usize) as *mut f32;
        let mut tgt = [*tgt_pos, *tgt_pos.add(1), *tgt_pos.add(2)];
        if tgt[0].is_finite() && tgt[1].is_finite() && tgt[2].is_finite() {
            Self::rotate_xy(&mut tgt, true, ox, oy, yaw);
            if tgt[0].is_finite() && tgt[1].is_finite() && tgt[2].is_finite() {
                *tgt_pos = tgt[0];
                *tgt_pos.add(1) = tgt[1];
                *tgt_pos.add(2) = tgt[2];
            }
        }
    }

    /// Extracts current vehicle speed in meters per second (m/s) from guest memory.
    #[allow(dead_code)]
    pub unsafe fn get_vehicle_speed_mps(guest_base: *const u8, car: u32) -> f32 {
        if guest_base.is_null() || car < 0x10000 || car >= 0x7ffff000 {
            return 0.0;
        }
        // First try CAR_SPEED (0x430)
        let spd_ptr = guest_base.add((car + CAR_SPEED_OFFSET) as usize) as *const f32;
        let raw_spd = *spd_ptr;
        if raw_spd.is_finite() && raw_spd >= 0.0 && raw_spd < 200.0 {
            return raw_spd;
        }
        // Fallback: magnitude of linear velocity vector (vx, vy, vz) at 0x70
        let vx = *(guest_base.add((car + CAR_VEL_OFFSET) as usize) as *const f32);
        let vy = *(guest_base.add((car + CAR_VEL_OFFSET + 4) as usize) as *const f32);
        let vz = *(guest_base.add((car + CAR_VEL_OFFSET + 8) as usize) as *const f32);
        if vx.is_finite() && vy.is_finite() && vz.is_finite() {
            let v_len = (vx * vx + vy * vy + vz * vz).sqrt();
            if v_len.is_finite() && v_len < 200.0 {
                return v_len;
            }
        }
        0.0
    }

    /// Restores original camera bytes in guest memory after frame rendering completes.
    pub unsafe fn restore_frame(&self, guest_base: *mut u8) {
        if guest_base.is_null() {
            return;
        }

        // Always restore motion blur parameters if they were modified
        if self.motion_blur_modified.swap(false, Ordering::AcqRel) {
            let mb_ptr = guest_base.add(G_MOTION_BLUR as usize) as *mut u32;
            let mb_weight_ptr = guest_base.add(G_MOTION_BLUR_WEIGHT as usize) as *mut f32;
            let orig_mb = self.saved_motion_blur_val.load(Ordering::Acquire);
            let orig_weight = f32::from_bits(self.saved_blur_weight.load(Ordering::Acquire));
            *mb_ptr = orig_mb;
            *mb_weight_ptr = orig_weight;
        }

        if !self.is_applied.swap(false, Ordering::AcqRel) {
            return;
        }

        let cam = self.turned_cam_addr.load(Ordering::Acquire);
        if cam == 0 {
            return;
        }

        let cam_raw = guest_base.add(cam as usize);
        if let Ok(guard) = self.saved_cam_bytes.lock() {
            std::ptr::copy_nonoverlapping(guard.as_ptr(), cam_raw, CAM_SIZE);
        }
    }

    /// Optionally rotates an active D3DTS_VIEW matrix directly if needed.
    pub unsafe fn transform_view_matrix(&self, matrix: *mut f32, guest_base: *const u8) -> bool {
        if !self.is_enabled() || matrix.is_null() || guest_base.is_null() {
            return false;
        }

        // If roam_android is loaded, camera orbit is handled directly in sub_005c7ec0.
        // Do not rotate D3DTS_VIEW again to prevent double-rotation.
        static HAS_ROAM: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
        let has_roam = *HAS_ROAM.get_or_init(|| unsafe {
            let sym = libc::dlsym(libc::RTLD_DEFAULT, b"nfs_is_camera_turned_in_guest\0".as_ptr() as *const _);
            !sym.is_null()
        });
        if has_roam {
            return false;
        }

        // If apply_frame already rotated this camera matrix in guest memory, do not double-rotate
        let turned_cam = self.turned_cam_addr.load(Ordering::Acquire);
        if turned_cam != 0 && self.is_applied.load(Ordering::Acquire) {
            let turned_ptr = guest_base.add(turned_cam as usize) as *const f32;
            if matrix as *const f32 == turned_ptr {
                return false;
            }
        }

        let yaw = self.get_yaw();
        if yaw.abs() < REST_YAW_THRESHOLD {
            return false;
        }

        let pl_ptr = guest_base.add(SPLIT_PLAYERS_INDEX as usize) as *const u32;
        let pl = *pl_ptr;
        if pl == 0 {
            return false;
        }

        let car_ptr = guest_base.add((pl + 4) as usize) as *const u32;
        let car = *car_ptr;
        if car == 0 {
            return false;
        }

        let car_pos = guest_base.add((car + CAR_POS_OFFSET) as usize) as *const f32;
        let ox = *car_pos;
        let oy = *car_pos.add(1);

        let mut m = [0.0f32; 16];
        std::ptr::copy_nonoverlapping(matrix, m.as_mut_ptr(), 16);
        Self::rotate_matrix(&mut m, ox, oy, yaw);
        std::ptr::copy_nonoverlapping(m.as_ptr(), matrix, 16);
        true
    }
}

pub static GLOBAL_CAMERA_STICK: CameraStickEngine = CameraStickEngine::new();

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn nfs_camera_stick_input(x: f32, y: f32) {
    GLOBAL_CAMERA_STICK.set_stick_input(x, y);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_stick_apply_frame(guest_base: *mut std::ffi::c_void) {
    GLOBAL_CAMERA_STICK.apply_frame(guest_base as *mut u8);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_stick_restore_frame(guest_base: *mut std::ffi::c_void) {
    GLOBAL_CAMERA_STICK.restore_frame(guest_base as *mut u8);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_camera_stick_transform_view_matrix(
    matrix: *mut f32,
    guest_base: *const std::ffi::c_void,
) -> i32 {
    if GLOBAL_CAMERA_STICK.transform_view_matrix(matrix, guest_base as *const u8) {
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn nfs_camera_stick_set_enabled(enabled: i32) {
    GLOBAL_CAMERA_STICK.set_enabled(enabled != 0);
}

#[no_mangle]
pub extern "C" fn nfs_camera_stick_get_yaw() -> f32 {
    GLOBAL_CAMERA_STICK.get_yaw()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deadzone_and_target_yaw() {
        let engine = CameraStickEngine::new();

        // Inside deadzone
        let (yaw, is_rear) = engine.compute_target_yaw(0.05, 0.05, 0.0);
        assert_eq!(yaw, 0.0);
        assert!(!is_rear);

        // Pointing full right (x = 1.0, y = 0.0) -> yaw ~ +pi/2
        let (yaw_right, _) = engine.compute_target_yaw(1.0, 0.0, 0.0);
        assert!((yaw_right - PI_F / 2.0).abs() < 0.01);

        // Pointing full left (x = -1.0, y = 0.0) -> yaw ~ -pi/2
        let (yaw_left, _) = engine.compute_target_yaw(-1.0, 0.0, 0.0);
        assert!((yaw_left - (-PI_F / 2.0)).abs() < 0.01);

        // Straight back (x = 0.0, y = 1.0) -> rear snap
        let (yaw_rear, is_rear) = engine.compute_target_yaw(0.0, 1.0, 0.0);
        assert!(is_rear);
        assert!((yaw_rear.abs() - PI_F).abs() < 0.01);
    }

    #[test]
    fn test_matrix_rotation_identity() {
        let mut m = [
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        ];
        // 0 yaw rotation should preserve matrix
        CameraStickEngine::rotate_matrix(&mut m, 10.0, 20.0, 0.0);
        assert_eq!(m[0], 1.0);
        assert_eq!(m[5], 1.0);
        assert_eq!(m[10], 1.0);
        assert_eq!(m[15], 1.0);
    }
}
