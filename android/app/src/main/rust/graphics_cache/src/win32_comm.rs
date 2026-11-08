//! Win32 Communications & Serial Port (kernel32.dll) Emulation in Rust for NFSU2 Android.
//!
//! Provides communications device configuration, DCB (Device Control Block) setup,
//! timeout management, event masking, and overlapped I/O status reporting.

use std::collections::HashMap;
use std::sync::Mutex;

// Comm / Serial Status Codes
pub const TRUE: i32 = 1;
pub const FALSE: i32 = 0;

// DCB Flags and Defaults
pub const NOPARITY: u8 = 0;
pub const ODDPARITY: u8 = 1;
pub const EVENPARITY: u8 = 2;
pub const MARKPARITY: u8 = 3;
pub const SPACEPARITY: u8 = 4;

pub const ONESTOPBIT: u8 = 0;
pub const ONE5STOPBITS: u8 = 1;
pub const TWOSTOPBITS: u8 = 2;

pub const CBR_110: u32 = 110;
pub const CBR_300: u32 = 300;
pub const CBR_600: u32 = 600;
pub const CBR_1200: u32 = 1200;
pub const CBR_2400: u32 = 2400;
pub const CBR_4800: u32 = 4800;
pub const CBR_9600: u32 = 9600;
pub const CBR_14400: u32 = 14400;
pub const CBR_19200: u32 = 19200;
pub const CBR_38400: u32 = 38400;
pub const CBR_57600: u32 = 57600;
pub const CBR_115200: u32 = 115200;

// Comm Events
pub const EV_RXCHAR: u32 = 0x0001;
pub const EV_RXFLAG: u32 = 0x0002;
pub const EV_TXEMPTY: u32 = 0x0004;
pub const EV_CTS: u32 = 0x0008;
pub const EV_DSR: u32 = 0x0010;
pub const EV_RLSD: u32 = 0x0020;
pub const EV_BREAK: u32 = 0x0040;
pub const EV_ERR: u32 = 0x0080;
pub const EV_RING: u32 = 0x0100;
pub const EV_PERR: u32 = 0x0200;
pub const EV_RX80FULL: u32 = 0x0400;
pub const EV_EVENT1: u32 = 0x0800;
pub const EV_EVENT2: u32 = 0x1000;

// PurgeComm Flags
pub const PURGE_TXABORT: u32 = 0x0001;
pub const PURGE_RXABORT: u32 = 0x0002;
pub const PURGE_TXCLEAR: u32 = 0x0004;
pub const PURGE_RXCLEAR: u32 = 0x0008;

/// Win32 DCB representation (28 bytes standard minimum).
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Win32Dcb {
    pub dcb_length: u32,
    pub baud_rate: u32,
    pub flags: u32, // Bitfields: fBinary, fParity, fOutxCtsFlow, etc.
    pub w_reserved: u16,
    pub xon_lim: u16,
    pub xoff_lim: u16,
    pub byte_size: u8,
    pub parity: u8,
    pub stop_bits: u8,
    pub xon_char: u8,
    pub xoff_char: u8,
    pub error_char: u8,
    pub eof_char: u8,
    pub evt_char: u8,
    pub w_reserved1: u16,
}

/// Win32 COMMTIMEOUTS representation (20 bytes).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Win32CommTimeouts {
    pub read_interval_timeout: u32,
    pub read_total_timeout_multiplier: u32,
    pub read_total_timeout_constant: u32,
    pub write_total_timeout_multiplier: u32,
    pub write_total_timeout_constant: u32,
}

/// State tracking for an active comm/serial port handle.
#[derive(Debug, Clone)]
pub struct CommPortState {
    pub baud_rate: u32,
    pub byte_size: u8,
    pub parity: u8,
    pub stop_bits: u8,
    pub in_queue_size: u32,
    pub out_queue_size: u32,
    pub event_mask: u32,
    pub timeouts: Win32CommTimeouts,
}

impl Default for CommPortState {
    fn default() -> Self {
        Self {
            baud_rate: CBR_115200,
            byte_size: 8,
            parity: NOPARITY,
            stop_bits: ONESTOPBIT,
            in_queue_size: 4096,
            out_queue_size: 4096,
            event_mask: 0,
            timeouts: Win32CommTimeouts::default(),
        }
    }
}

static COMM_MANAGER: Mutex<Option<HashMap<u32, CommPortState>>> = Mutex::new(None);

fn with_comm_map<F, R>(f: F) -> R
where
    F: FnOnce(&mut HashMap<u32, CommPortState>) -> R,
{
    let mut guard = COMM_MANAGER.lock().unwrap();
    let map = guard.get_or_insert_with(HashMap::new);
    f(map)
}

/// Configures communications device input and output buffer sizes.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_setup(handle: u32, in_queue: u32, out_queue: u32) -> i32 {
    with_comm_map(|map| {
        let entry = map.entry(handle).or_default();
        entry.in_queue_size = in_queue;
        entry.out_queue_size = out_queue;
    });
    TRUE
}

/// Sets communications parameters according to a DCB structure.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_set_state(
    handle: u32,
    dcb_ptr: *const u8,
    dcb_size: usize,
) -> i32 {
    if dcb_ptr.is_null() || dcb_size < std::mem::size_of::<Win32Dcb>() {
        return FALSE;
    }

    let dcb = &*(dcb_ptr as *const Win32Dcb);
    with_comm_map(|map| {
        let entry = map.entry(handle).or_default();
        entry.baud_rate = dcb.baud_rate;
        entry.byte_size = dcb.byte_size;
        entry.parity = dcb.parity;
        entry.stop_bits = dcb.stop_bits;
    });

    TRUE
}

/// Populates a DCB structure with active communications parameters.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_get_state(
    handle: u32,
    dcb_ptr: *mut u8,
    dcb_size: usize,
) -> i32 {
    let req_size = std::mem::size_of::<Win32Dcb>();
    if dcb_ptr.is_null() || dcb_size < req_size {
        return FALSE;
    }

    let state = with_comm_map(|map| map.get(&handle).cloned().unwrap_or_default());

    let dcb = &mut *(dcb_ptr as *mut Win32Dcb);
    dcb.dcb_length = req_size as u32;
    dcb.baud_rate = state.baud_rate;
    dcb.flags = 0x00000001; // fBinary = 1
    dcb.w_reserved = 0;
    dcb.xon_lim = 2048;
    dcb.xoff_lim = 512;
    dcb.byte_size = state.byte_size;
    dcb.parity = state.parity;
    dcb.stop_bits = state.stop_bits;
    dcb.xon_char = 0x11;
    dcb.xoff_char = 0x13;
    dcb.error_char = 0;
    dcb.eof_char = 0;
    dcb.evt_char = 0;
    dcb.w_reserved1 = 0;

    TRUE
}

/// Sets the current communications configuration.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_set_config(
    handle: u32,
    config_ptr: *const u8,
    size: u32,
) -> i32 {
    if config_ptr.is_null() || size < 4 {
        return FALSE;
    }
    // Minimal valid COMMCONFIG header contains dwSize at offset 0
    let reported_size = *(config_ptr as *const u32);
    if reported_size > size {
        return FALSE;
    }
    with_comm_map(|map| {
        map.entry(handle).or_default();
    });
    TRUE
}

/// Queries the current communications configuration.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_get_config(
    handle: u32,
    config_ptr: *mut u8,
    size_ptr: *mut u32,
) -> i32 {
    if size_ptr.is_null() {
        return FALSE;
    }
    let available_size = *size_ptr;
    let min_size = 52u32; // Standard COMMCONFIG structure size

    *size_ptr = min_size;
    if config_ptr.is_null() || available_size < min_size {
        return FALSE;
    }

    std::ptr::write_bytes(config_ptr, 0, min_size as usize);
    let config_u32 = config_ptr as *mut u32;
    *config_u32.offset(0) = min_size; // dwSize
    *config_u32.offset(1) = 1;        // wVersion
    *config_u32.offset(2) = 1;        // dwProviderSubType (PST_RS232)

    with_comm_map(|map| {
        map.entry(handle).or_default();
    });

    TRUE
}

/// Sets the timeout parameters for all read/write operations on a comm port.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_set_timeouts(
    handle: u32,
    timeouts_ptr: *const u8,
    size: usize,
) -> i32 {
    if timeouts_ptr.is_null() || size < std::mem::size_of::<Win32CommTimeouts>() {
        return FALSE;
    }
    let timeouts = *(timeouts_ptr as *const Win32CommTimeouts);
    with_comm_map(|map| {
        let entry = map.entry(handle).or_default();
        entry.timeouts = timeouts;
    });
    TRUE
}

/// Specifies a set of events to be monitored for a communications device.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_set_mask(handle: u32, mask: u32) -> i32 {
    with_comm_map(|map| {
        let entry = map.entry(handle).or_default();
        entry.event_mask = mask;
    });
    TRUE
}

/// Waits for an event to occur for a specified communications device.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_wait_event(
    handle: u32,
    mask_out: *mut u32,
    _overlapped_ptr: *mut u8,
) -> i32 {
    if !mask_out.is_null() {
        let mask = with_comm_map(|map| map.get(&handle).map(|s| s.event_mask).unwrap_or(0));
        *mask_out = mask & (EV_RXCHAR | EV_TXEMPTY);
    }
    TRUE
}

/// Discards all characters from the output or input buffer of a specified comm resource.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_purge(_handle: u32, _flags: u32) -> i32 {
    TRUE
}

/// Retrieves the results of an overlapped operation on the specified file/comm port.
#[no_mangle]
pub unsafe extern "C" fn rust_comm_get_overlapped_result(
    _handle: u32,
    overlapped_ptr: *const u8,
    bytes_out: *mut u32,
    _wait: i32,
) -> i32 {
    if overlapped_ptr.is_null() {
        return FALSE;
    }
    if !bytes_out.is_null() {
        // InternalHigh at offset 4 holds bytes transferred
        let internal_high = *((overlapped_ptr as *const u8).offset(4) as *const u32);
        *bytes_out = internal_high;
    }
    TRUE
}
