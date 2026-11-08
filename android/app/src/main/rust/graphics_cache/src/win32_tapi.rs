//! Telephony API (tapi32.dll) Emulation in Rust for NFSU2 Android.
//!
//! Provides telephony line device lifecycle, modem negotiation, capability queries,
//! and call management stubs for legacy modem dialers.

use std::sync::atomic::{AtomicU32, Ordering};

// TAPI Return Codes
pub const SUCCESS: i32 = 0;
pub const LINEERR_INVALAPPHANDLE: i32 = -2147483627; // 0x80000015
pub const LINEERR_INVALDEVICEID: i32 = -2147483621;  // 0x8000001B
pub const LINEERR_NODRIVER: i32 = -2147483606;       // 0x8000002A
pub const LINEERR_OPERATIONFAILED: i32 = -2147483600;// 0x80000030
pub const LINEERR_STRUCTURETOOSMALL: i32 = -2147483597; // 0x80000033

pub const TAPI_CURRENT_VERSION: u32 = 0x00020000; // TAPI 2.0
pub const LINEMEDIAMODE_DATAMODEM: u32 = 0x00000008;

static NEXT_LINE_APP: AtomicU32 = AtomicU32::new(0x71000001);
static NEXT_LINE: AtomicU32 = AtomicU32::new(0x72000001);
static NEXT_CALL: AtomicU32 = AtomicU32::new(0x73000001);

/// Initializes the application's use of TAPI.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_initialize(
    app_out: *mut u32,
    _instance: u32,
    _callback: u32,
    _name_ptr: *const u8,
    num_devs_out: *mut u32,
) -> i32 {
    if app_out.is_null() || num_devs_out.is_null() {
        return LINEERR_OPERATIONFAILED;
    }

    let app_handle = NEXT_LINE_APP.fetch_add(1, Ordering::Relaxed);
    *app_out = app_handle;
    // Report 0 modem devices on Android host (graceful no-modem signal)
    *num_devs_out = 0;

    SUCCESS
}

/// Shuts down the application's use of the line abstraction of the API.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_shutdown(app: u32) -> i32 {
    if app == 0 {
        return LINEERR_INVALAPPHANDLE;
    }
    SUCCESS
}

/// Opens the line device specified by its device identifier.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_open(
    _app: u32,
    _dev_id: u32,
    line_out: *mut u32,
    _api_version: u32,
    _ext_version: u32,
    _callback_inst: u32,
    _privileges: u32,
    _media_modes: u32,
    _call_params: *const u8,
) -> i32 {
    if !line_out.is_null() {
        *line_out = NEXT_LINE.fetch_add(1, Ordering::Relaxed);
    }
    SUCCESS
}

/// Closes the specified open line device.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_close(line: u32) -> i32 {
    if line == 0 {
        return LINEERR_OPERATIONFAILED;
    }
    SUCCESS
}

/// Queries a specified line device to determine its telephony capabilities.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_get_dev_caps(
    _app: u32,
    _dev_id: u32,
    _api_ver: u32,
    _ext_ver: u32,
    dev_caps_out: *mut u8,
    caps_size: usize,
) -> i32 {
    if dev_caps_out.is_null() || caps_size < 16 {
        return LINEERR_STRUCTURETOOSMALL;
    }

    std::ptr::write_bytes(dev_caps_out, 0, caps_size);
    let u32_ptr = dev_caps_out as *mut u32;
    *u32_ptr.offset(0) = caps_size as u32;        // dwTotalSize
    *u32_ptr.offset(1) = 16;                     // dwNeededSize
    *u32_ptr.offset(2) = 16;                     // dwUsedSize
    *u32_ptr.offset(3) = LINEMEDIAMODE_DATAMODEM;// dwMediaModes

    SUCCESS
}

/// Returns a device identifier for the specified device class associated with the line/call.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_get_id(
    _line: u32,
    _address_id: u32,
    _call: u32,
    _select: u32,
    var_string_out: *mut u8,
    string_size: usize,
    _device_class: *const u8,
) -> i32 {
    if var_string_out.is_null() || string_size < 16 {
        return LINEERR_STRUCTURETOOSMALL;
    }

    std::ptr::write_bytes(var_string_out, 0, string_size);
    let u32_ptr = var_string_out as *mut u32;
    *u32_ptr.offset(0) = string_size as u32; // dwTotalSize
    *u32_ptr.offset(1) = 16;                // dwNeededSize
    *u32_ptr.offset(2) = 16;                // dwUsedSize
    *u32_ptr.offset(3) = 0;                 // dwStringFormat (STRINGFORMAT_BINARY)

    SUCCESS
}

/// Negotiates an API extension version to use with the specified line device.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_negotiate_api_version(
    _app: u32,
    _dev_id: u32,
    low_ver: u32,
    high_ver: u32,
    ver_out: *mut u32,
    ext_out: *mut u8,
) -> i32 {
    if ver_out.is_null() {
        return LINEERR_OPERATIONFAILED;
    }

    let chosen_ver = if low_ver <= TAPI_CURRENT_VERSION && high_ver >= TAPI_CURRENT_VERSION {
        TAPI_CURRENT_VERSION
    } else {
        low_ver
    };

    *ver_out = chosen_ver;

    if !ext_out.is_null() {
        std::ptr::write_bytes(ext_out, 0, 16);
    }

    SUCCESS
}

/// Places a call on the specified line to the specified destination address.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_make_call(
    _line: u32,
    call_out: *mut u32,
    _dest_addr: *const u8,
    _country: u32,
    _call_params: *const u8,
) -> i32 {
    if !call_out.is_null() {
        *call_out = NEXT_CALL.fetch_add(1, Ordering::Relaxed);
    }
    SUCCESS
}

/// Answers the specified offering call.
#[no_mangle]
pub unsafe extern "C" fn rust_tapi_line_answer(
    call: u32,
    _user_info: *const u8,
    _size: u32,
) -> i32 {
    if call == 0 {
        return LINEERR_OPERATIONFAILED;
    }
    SUCCESS
}
