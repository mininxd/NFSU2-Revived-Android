//! DirectDraw (ddraw.dll) Emulation Module in Rust for NFSU2 Android.
//!
//! Provides display mode enumeration, surface capability queries, cooperative level
//! configuration, and mock IDirectDraw / IDirectDraw7 COM interface handling for
//! legacy display hardware detection in SPEED2.EXE.

use std::os::raw::{c_char, c_int, c_void};
use std::slice;

// DirectDraw HRESULT Status Codes
pub const DD_OK: u32 = 0x00000000;
pub const DDERR_GENERIC: u32 = 0x80004005;
pub const DDERR_INVALIDPARAMS: u32 = 0x80004003;
pub const DDERR_UNSUPPORTED: u32 = 0x80004001;
pub const DDERR_ALREADYINITIALIZED: u32 = 0x88760000 + 5;
pub const DDERR_NOCOOPERATIVELEVELSET: u32 = 0x88760000 + 212;

// DDSURFACEDESC Flags
pub const DDSD_CAPS: u32 = 0x00000001;
pub const DDSD_HEIGHT: u32 = 0x00000002;
pub const DDSD_WIDTH: u32 = 0x00000004;
pub const DDSD_PITCH: u32 = 0x00000008;
pub const DDSD_PIXELFORMAT: u32 = 0x00001000;
pub const DDSD_REFRESHRATE: u32 = 0x00040000;

// DDPIXELFORMAT Flags
pub const DDPF_RGB: u32 = 0x00000040;

// DirectDraw Cooperative Level Flags
pub const DDSCL_FULLSCREEN: u32 = 0x00000001;
pub const DDSCL_ALLOWREBOOT: u32 = 0x00000002;
pub const DDSCL_NOWINDOWCHANGES: u32 = 0x00000004;
pub const DDSCL_NORMAL: u32 = 0x00000008;
pub const DDSCL_EXCLUSIVE: u32 = 0x00000010;
pub const DDSCL_ALLOWMODEX: u32 = 0x00000040;

// DirectDraw Capabilities Flags
pub const DDCAPS_BLT: u32 = 0x00000040;
pub const DDCAPS_3D: u32 = 0x00000001;
pub const DDCAPS_COLORKEY: u32 = 0x00000200;

/// DirectDraw Surface Description structure matching Win32 DDSURFACEDESC (108 bytes)
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DdSurfaceDesc {
    pub dw_size: u32,
    pub dw_flags: u32,
    pub dw_height: u32,
    pub dw_width: u32,
    pub l_pitch: i32,
    pub dw_back_buffer_count: u32,
    pub dw_refresh_rate: u32,
    pub dw_alpha_bit_depth: u32,
    pub dw_reserved: u32,
    pub lp_surface: u32,
    pub ddck_ck_dest_overlay: [u32; 2],
    pub ddck_ck_dest_blt: [u32; 2],
    pub ddck_ck_src_overlay: [u32; 2],
    pub ddck_ck_src_blt: [u32; 2],
    // DdPixelFormat embedded (32 bytes)
    pub pf_size: u32,
    pub pf_flags: u32,
    pub pf_four_cc: u32,
    pub pf_rgb_bit_count: u32,
    pub pf_r_bit_mask: u32,
    pub pf_g_bit_mask: u32,
    pub pf_b_bit_mask: u32,
    pub pf_rgb_alpha_bit_mask: u32,
    // DdSCaps embedded (16 bytes)
    pub dds_caps: u32,
}

impl Default for DdSurfaceDesc {
    fn default() -> Self {
        Self {
            dw_size: 108,
            dw_flags: DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE,
            dw_height: 720,
            dw_width: 1280,
            l_pitch: 1280 * 4,
            dw_back_buffer_count: 1,
            dw_refresh_rate: 60,
            dw_alpha_bit_depth: 8,
            dw_reserved: 0,
            lp_surface: 0,
            ddck_ck_dest_overlay: [0, 0],
            ddck_ck_dest_blt: [0, 0],
            ddck_ck_src_overlay: [0, 0],
            ddck_ck_src_blt: [0, 0],
            pf_size: 32,
            pf_flags: DDPF_RGB,
            pf_four_cc: 0,
            pf_rgb_bit_count: 32,
            pf_r_bit_mask: 0x00FF0000,
            pf_g_bit_mask: 0x0000FF00,
            pf_b_bit_mask: 0x000000FF,
            pf_rgb_alpha_bit_mask: 0xFF000000,
            dds_caps: 0x00001000, // DDSCAPS_PRIMARYSURFACE
        }
    }
}

/// Populates a Win32 DDSURFACEDESC memory block with the active display mode.
#[no_mangle]
pub unsafe extern "C" fn rust_ddraw_get_display_mode(
    desc_buf: *mut u8,
    buf_size: usize,
    width: u32,
    height: u32,
    refresh_rate: u32,
) -> u32 {
    if desc_buf.is_null() || buf_size < 108 {
        return DDERR_INVALIDPARAMS;
    }

    let actual_w = if width == 0 { 1280 } else { width };
    let actual_h = if height == 0 { 720 } else { height };
    let actual_hz = if refresh_rate == 0 { 60 } else { refresh_rate };

    let mut desc = DdSurfaceDesc::default();
    desc.dw_width = actual_w;
    desc.dw_height = actual_h;
    desc.l_pitch = (actual_w * 4) as i32;
    desc.dw_refresh_rate = actual_hz;

    let src_slice = slice::from_raw_parts(&desc as *const DdSurfaceDesc as *const u8, 108);
    let dst_slice = slice::from_raw_parts_mut(desc_buf, 108);
    dst_slice.copy_from_slice(src_slice);

    DD_OK
}

/// Fills DirectDraw Driver & HEL Capabilities block (DDCAPS).
#[no_mangle]
pub unsafe extern "C" fn rust_ddraw_get_caps(
    driver_caps_buf: *mut u8,
    driver_caps_size: usize,
    hel_caps_buf: *mut u8,
    hel_caps_size: usize,
) -> u32 {
    if !driver_caps_buf.is_null() && driver_caps_size >= 40 {
        let dst = slice::from_raw_parts_mut(driver_caps_buf, driver_caps_size);
        dst.fill(0);
        // dwSize at offset 0
        let size_bytes = (driver_caps_size as u32).to_le_bytes();
        dst[0..4].copy_from_slice(&size_bytes);
        // dwCaps at offset 4: Hardware Blt, 3D acceleration, ColorKey
        let caps = (DDCAPS_BLT | DDCAPS_3D | DDCAPS_COLORKEY).to_le_bytes();
        dst[4..8].copy_from_slice(&caps);
    }

    if !hel_caps_buf.is_null() && hel_caps_size >= 40 {
        let dst = slice::from_raw_parts_mut(hel_caps_buf, hel_caps_size);
        dst.fill(0);
        let size_bytes = (hel_caps_size as u32).to_le_bytes();
        dst[0..4].copy_from_slice(&size_bytes);
        let caps = DDCAPS_BLT.to_le_bytes();
        dst[4..8].copy_from_slice(&caps);
    }

    DD_OK
}

/// Validates cooperative level requested by the game.
#[no_mangle]
pub unsafe extern "C" fn rust_ddraw_set_cooperative_level(
    hwnd: u32,
    flags: u32,
) -> u32 {
    let _ = hwnd;
    let _ = flags;
    // Android is always fullscreen and owns surface lifecycle through NativeActivity/ANativeWindow.
    DD_OK
}

/// Standard resolutions for DirectDraw display mode enumeration.
pub const DDRAW_STANDARD_MODES: &[(u32, u32, u32, u32)] = &[
    (640, 480, 32, 60),
    (800, 600, 32, 60),
    (1024, 768, 32, 60),
    (1280, 720, 32, 60),
    (1280, 1024, 32, 60),
    (1600, 900, 32, 60),
    (1920, 1080, 32, 60),
    (2560, 1440, 32, 60),
];

/// Returns the number of standard display modes available for enumeration.
#[no_mangle]
pub extern "C" fn rust_ddraw_get_mode_count() -> u32 {
    DDRAW_STANDARD_MODES.len() as u32
}

/// Copies the display mode at `index` into a DDSURFACEDESC buffer.
#[no_mangle]
pub unsafe extern "C" fn rust_ddraw_get_mode_at(
    index: u32,
    desc_buf: *mut u8,
    buf_size: usize,
) -> u32 {
    if desc_buf.is_null() || buf_size < 108 {
        return DDERR_INVALIDPARAMS;
    }
    let idx = index as usize;
    if idx >= DDRAW_STANDARD_MODES.len() {
        return DDERR_INVALIDPARAMS;
    }
    let (w, h, bpp, hz) = DDRAW_STANDARD_MODES[idx];
    let mut desc = DdSurfaceDesc::default();
    desc.dw_width = w;
    desc.dw_height = h;
    desc.l_pitch = (w * 4) as i32;
    desc.dw_refresh_rate = hz;
    desc.pf_rgb_bit_count = bpp;

    let src = slice::from_raw_parts(&desc as *const DdSurfaceDesc as *const u8, 108);
    let dst = slice::from_raw_parts_mut(desc_buf, 108);
    dst.copy_from_slice(src);
    DD_OK
}
