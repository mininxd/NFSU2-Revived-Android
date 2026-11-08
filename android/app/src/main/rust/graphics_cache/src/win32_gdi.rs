//! Graphics Device Interface (gdi32.dll) Emulation in Rust for NFSU2 Android.
//!
//! Provides device contexts, software surface blits, font creation, and text
//! rendering emulation for game error dialogs and diagnostics.

use std::collections::HashMap;
use std::sync::Mutex;
use std::sync::atomic::{AtomicU32, Ordering};

// GDI Constants
pub const CLR_INVALID: u32 = 0xFFFFFFFF;
pub const OPAQUE: i32 = 2;
pub const TRANSPARENT: i32 = 1;
pub const SRCCOPY: u32 = 0x00CC0020;

static NEXT_DC: AtomicU32 = AtomicU32::new(0x74000001);
static NEXT_FONT: AtomicU32 = AtomicU32::new(0x75000001);
static NEXT_BITMAP: AtomicU32 = AtomicU32::new(0x76000001);

#[derive(Debug, Clone)]
pub struct DeviceContext {
    pub text_color: u32,
    pub bk_color: u32,
    pub bk_mode: i32,
    pub selected_font: u32,
    pub selected_bitmap: u32,
}

impl Default for DeviceContext {
    fn default() -> Self {
        Self {
            text_color: 0x00000000, // Black
            bk_color: 0x00FFFFFF,   // White
            bk_mode: OPAQUE,
            selected_font: 0,
            selected_bitmap: 0,
        }
    }
}

static GDI_DCS: Mutex<Option<HashMap<u32, DeviceContext>>> = Mutex::new(None);

fn with_dcs<F, R>(f: F) -> R
where
    F: FnOnce(&mut HashMap<u32, DeviceContext>) -> R,
{
    let mut guard = GDI_DCS.lock().unwrap();
    let map = guard.get_or_insert_with(HashMap::new);
    f(map)
}

/// Creates a memory device context compatible with the specified device.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_create_compatible_dc(_hdc: u32) -> u32 {
    let dc_handle = NEXT_DC.fetch_add(1, Ordering::Relaxed);
    with_dcs(|dcs| {
        dcs.insert(dc_handle, DeviceContext::default());
    });
    dc_handle
}

/// Deletes the specified device context.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_delete_dc(hdc: u32) -> i32 {
    let removed = with_dcs(|dcs| dcs.remove(&hdc).is_some());
    if removed { 1 } else { 0 }
}

/// Creates a logical font with the specified characteristics.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_create_font_a(
    _height: i32,
    _width: i32,
    _escapement: i32,
    _orientation: i32,
    _weight: i32,
    _italic: u32,
    _underline: u32,
    _strikeout: u32,
    _charset: u32,
    _out_prec: u32,
    _clip_prec: u32,
    _quality: u32,
    _pitch: u32,
    _face: *const u8,
) -> u32 {
    NEXT_FONT.fetch_add(1, Ordering::Relaxed)
}

/// Creates a bitmap with the specified width, height, and color format.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_create_bitmap(
    _width: i32,
    _height: i32,
    _planes: u32,
    _bit_count: u32,
    _bits: *const u8,
) -> u32 {
    NEXT_BITMAP.fetch_add(1, Ordering::Relaxed)
}

/// Draws text using the currently selected font, background color, and text color.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_ext_text_out_a(
    hdc: u32,
    _x: i32,
    _y: i32,
    _options: u32,
    _rect: *const i32,
    _text: *const u8,
    _len: u32,
    _dx: *const i32,
) -> i32 {
    let exists = with_dcs(|dcs| dcs.contains_key(&hdc) || hdc != 0);
    if exists { 1 } else { 0 }
}

/// Selects an object into the specified device context.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_select_object(hdc: u32, obj: u32) -> u32 {
    with_dcs(|dcs| {
        let entry = dcs.entry(hdc).or_default();
        if (obj & 0xFF000000) == 0x75000000 {
            let prev = entry.selected_font;
            entry.selected_font = obj;
            prev
        } else if (obj & 0xFF000000) == 0x76000000 {
            let prev = entry.selected_bitmap;
            entry.selected_bitmap = obj;
            prev
        } else {
            obj
        }
    })
}

/// Performs a bit-block transfer of the color data from a source DC into a destination DC.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_bit_blt(
    hdc_dest: u32,
    _x: i32,
    _y: i32,
    _cx: i32,
    _cy: i32,
    _hdc_src: u32,
    _x1: i32,
    _y1: i32,
    _rop: u32,
) -> i32 {
    let exists = with_dcs(|dcs| dcs.contains_key(&hdc_dest) || hdc_dest != 0);
    if exists { 1 } else { 0 }
}

/// Retrieves the red, green, blue (RGB) color value of the pixel at the specified coordinates.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_get_pixel(_hdc: u32, _x: i32, _y: i32) -> u32 {
    // Return standard dark gray or black pixel
    0x00202020
}

/// Sets the text color for the specified device context to the specified color.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_set_text_color(hdc: u32, color: u32) -> u32 {
    with_dcs(|dcs| {
        let entry = dcs.entry(hdc).or_default();
        let prev = entry.text_color;
        entry.text_color = color;
        prev
    })
}

/// Sets the current background color for the specified device context.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_set_bk_color(hdc: u32, color: u32) -> u32 {
    with_dcs(|dcs| {
        let entry = dcs.entry(hdc).or_default();
        let prev = entry.bk_color;
        entry.bk_color = color;
        prev
    })
}

/// Sets the current background mix mode for the specified device context.
#[no_mangle]
pub unsafe extern "C" fn rust_gdi_set_bk_mode(hdc: u32, mode: i32) -> i32 {
    with_dcs(|dcs| {
        let entry = dcs.entry(hdc).or_default();
        let prev = entry.bk_mode;
        entry.bk_mode = mode;
        prev
    })
}
