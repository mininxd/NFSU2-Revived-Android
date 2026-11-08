//! Netapi32 (netapi32.dll) Emulation Module in Rust for NFSU2 Android.
//!
//! Emulates Netbios NCB operations (LANA enumeration, adapter MAC queries, reset),
//! Workstation status (NetWkstaGetInfo), Domain/Workgroup join queries (NetGetJoinInformation),
//! and memory lifecycle management (NetApiBufferFree).

use std::ffi::CStr;
use std::os::raw::c_char;
use std::slice;

// Netbios Command Codes
pub const NCBENUM: u8 = 0x37;
pub const NCBRESET: u8 = 0x32;
pub const NCBASTAT: u8 = 0x33;
pub const NCBADDNAME: u8 = 0x30;
pub const NCBDELNAME: u8 = 0x31;

// Netbios Return Codes
pub const NRC_GOODRET: u8 = 0x00;
pub const NRC_BUFLEN: u8 = 0x01;
pub const NRC_ILLCMD: u8 = 0x03;
pub const NRC_CMDTMO: u8 = 0x05;
pub const NRC_INCOMP: u8 = 0x06;
pub const NRC_BADBRD: u8 = 0x07;
pub const NRC_SNUMOUT: u8 = 0x08;
pub const NRC_NORES: u8 = 0x09;
pub const NRC_INVADDRESS: u8 = 0x0a;
pub const NRC_NOWILD: u8 = 0x15;
pub const NRC_INUSE: u8 = 0x16;
pub const NRC_NAMERR: u8 = 0x17;
pub const NRC_ENVNOTDEF: u8 = 0x34;

// NetAPI / LanMan Return Codes
pub const NERR_SUCCESS: u32 = 0;
pub const ERROR_ACCESS_DENIED: u32 = 5;
pub const ERROR_NOT_SUPPORTED: u32 = 50;
pub const ERROR_INVALID_PARAMETER: u32 = 87;

// NetSetup Join Status
pub const NET_SETUP_UNKNOWN_STATUS: u32 = 0;
pub const NET_SETUP_UNJOINED: u32 = 1;
pub const NET_SETUP_WORKGROUP_NAME: u32 = 2;
pub const NET_SETUP_DOMAIN_NAME: u32 = 3;

/// Computes a stable, deterministic locally administered MAC address from seed.
pub fn generate_stable_mac(seed_str: Option<&str>) -> [u8; 6] {
    let mut hash: u32 = 0x811c9dc5; // FNV-1a 32-bit offset basis
    if let Some(s) = seed_str {
        for b in s.bytes() {
            hash = (hash ^ (b as u32)).wrapping_mul(0x01000193);
        }
    } else {
        hash = 0x55aa3377;
    }

    [
        0x02, // Locally administered unicast MAC
        0x4e, // 'N'
        (hash & 0xff) as u8,
        ((hash >> 8) & 0xff) as u8,
        ((hash >> 16) & 0xff) as u8,
        ((hash >> 24) & 0xff) as u8,
    ]
}

/// Dispatches a Netbios Control Block (NCB) call.
///
/// Returns status code to store in ncb_retcode and EAX.
#[no_mangle]
pub unsafe extern "C" fn rust_netapi_netbios(
    ncb_buf: *mut u8,
    ncb_len: usize,
    buffer_ptr: *mut u8,
    buffer_capacity: usize,
    seed_cstr: *const c_char,
) -> u8 {
    if ncb_buf.is_null() || ncb_len < 64 {
        return NRC_ILLCMD;
    }

    let ncb = slice::from_raw_parts_mut(ncb_buf, ncb_len);
    let raw_cmd = ncb[0];
    let command = raw_cmd & 0x7f; // Strip ASYNCH bit (0x80)

    let seed_str = if !seed_cstr.is_null() {
        CStr::from_ptr(seed_cstr).to_str().ok()
    } else {
        None
    };

    let status = match command {
        NCBENUM => {
            // LANA_ENUM: { UCHAR length; UCHAR lana[255]; }
            if buffer_ptr.is_null() || buffer_capacity < 2 {
                NRC_BUFLEN
            } else {
                let out = slice::from_raw_parts_mut(buffer_ptr, 2);
                out[0] = 1; // 1 LANA adapter available
                out[1] = 0; // LANA number 0
                NRC_GOODRET
            }
        }
        NCBRESET => {
            // NCBRESET: clears resources for specified LANA
            NRC_GOODRET
        }
        NCBASTAT => {
            // NCBASTAT: returns ADAPTER_STATUS (starts with 6-byte MAC address) followed by NAME_BUFFERs
            if buffer_ptr.is_null() || buffer_capacity < 6 {
                NRC_BUFLEN
            } else {
                let mac = generate_stable_mac(seed_str);
                let out = slice::from_raw_parts_mut(buffer_ptr, buffer_capacity);
                out.fill(0);
                out[0..6].copy_from_slice(&mac);

                // Set adapter type: Ethernet (0xfe)
                if buffer_capacity >= 60 {
                    out[6] = 0xfe; // adapter_type
                    out[7] = 0x00;
                    // Max data frames
                    let max_frames = 16u16.to_le_bytes();
                    out[16..18].copy_from_slice(&max_frames);
                }

                // Update NCB buffer length with bytes returned
                let written = std::cmp::min(buffer_capacity as u16, 60u16);
                ncb[8..10].copy_from_slice(&written.to_le_bytes());
                NRC_GOODRET
            }
        }
        NCBADDNAME | NCBDELNAME => {
            // Virtual name registration succeeds
            NRC_GOODRET
        }
        _ => {
            // Unhandled or unimplemented commands report NRC_GOODRET for non-fatal stubs
            NRC_GOODRET
        }
    };

    ncb[1] = status;  // ncb_retcode
    ncb[49] = status; // ncb_cmd_cplt

    status
}

/// Frees memory allocated by NetApi functions.
#[no_mangle]
pub extern "C" fn rust_netapi_buffer_free(buffer_addr: u32) -> u32 {
    let _ = buffer_addr;
    NERR_SUCCESS
}

/// Emulates NetWkstaGetInfo query for workstation and domain information.
#[no_mangle]
pub unsafe extern "C" fn rust_netapi_wksta_get_info(
    server_name: *const c_char,
    level: u32,
    out_buf: *mut u8,
    buf_size: usize,
    bytes_written: *mut usize,
) -> u32 {
    let _ = server_name;
    if out_buf.is_null() || buf_size < 32 {
        return ERROR_INVALID_PARAMETER;
    }

    let dst = slice::from_raw_parts_mut(out_buf, buf_size);
    dst.fill(0);

    // WKSTA_INFO_100:
    // DWORD wki100_platform_id (PLATFORM_ID_NT = 500)
    // LMSTR wki100_computername
    // LMSTR wki100_langroup
    // DWORD wki100_ver_major (5)
    // DWORD wki100_ver_minor (1)
    if level == 100 || level == 101 {
        dst[0..4].copy_from_slice(&500u32.to_le_bytes()); // PLATFORM_ID_NT
        dst[12..16].copy_from_slice(&5u32.to_le_bytes()); // Major: 5 (XP)
        dst[16..20].copy_from_slice(&1u32.to_le_bytes()); // Minor: 1
    }

    if !bytes_written.is_null() {
        *bytes_written = 32;
    }

    NERR_SUCCESS
}

/// Emulates NetGetJoinInformation returning standard workgroup status.
#[no_mangle]
pub unsafe extern "C" fn rust_netapi_get_join_information(
    out_join_status: *mut u32,
    out_name_buf: *mut u8,
    max_name_len: usize,
) -> u32 {
    if !out_join_status.is_null() {
        *out_join_status = NET_SETUP_WORKGROUP_NAME;
    }

    if !out_name_buf.is_null() && max_name_len >= 20 {
        // UTF-16 "WORKGROUP\0"
        let name_u16: [u16; 10] = [
            'W' as u16, 'O' as u16, 'R' as u16, 'K' as u16, 'G' as u16,
            'R' as u16, 'O' as u16, 'U' as u16, 'P' as u16, 0,
        ];
        let bytes: &[u8] = slice::from_raw_parts(name_u16.as_ptr() as *const u8, 20);
        let dst = slice::from_raw_parts_mut(out_name_buf, 20);
        dst.copy_from_slice(bytes);
    }

    NERR_SUCCESS
}
