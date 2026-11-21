//! Pure Rust Local TCP Bridge Protocol Engine for NFSU2 Online.
//! Handles zero-allocation TCP framing, parsing, and in-place address rewriting.

use jni::objects::{JByteArray, JClass, JIntArray};
use jni::sys::{jbyteArray, jint, jintArray, jsize};
use jni::JNIEnv;

pub const VHOST_VIP: u32 = 0x0A4D0001; // 10.77.0.1
pub const KIND_TCP: u8 = 4;

pub const TCP_OP_SYN: u8 = 1;
pub const TCP_OP_SYNACK: u8 = 2;
pub const TCP_OP_RST: u8 = 3;
pub const TCP_OP_DATA: u8 = 4;
pub const TCP_OP_FIN: u8 = 5;

pub const TARGET_IP_BYTES: &[u8] = b"10.77.0.1";
pub const REPLACEMENT_IP_BYTES: &[u8] = b"127.0.0.1";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedTcpFrame {
    pub src_vip: u32,
    pub dst_vip: u32,
    pub sport: u16,
    pub dport: u16,
    pub op: u8,
    pub ttl: u8,
    pub cid: i32,
    pub payload_offset: usize,
    pub payload_len: usize,
}

/// Encodes a 24-byte LAN TCP header followed by the payload into a frame buffer.
pub fn encode_tcp_frame(
    src_vip: u32,
    dst_vip: u32,
    sport: u16,
    dport: u16,
    op: u8,
    ttl: u8,
    cid: i32,
    payload: &[u8],
) -> Vec<u8> {
    let mut frame = Vec::with_capacity(24 + payload.len());
    // [0..3] Kind and padding
    frame.push(KIND_TCP);
    frame.push(0);
    frame.push(0);
    frame.push(0);

    // [4..7] src_vip (Big-Endian)
    frame.extend_from_slice(&src_vip.to_be_bytes());

    // [8..11] dst_vip (Big-Endian)
    frame.extend_from_slice(&dst_vip.to_be_bytes());

    // [12..13] sport (Big-Endian)
    frame.extend_from_slice(&sport.to_be_bytes());

    // [14..15] dport (Big-Endian)
    frame.extend_from_slice(&dport.to_be_bytes());

    // [16] op
    frame.push(op);

    // [17] ttl
    frame.push(ttl);

    // [18..19] pad
    frame.push(0);
    frame.push(0);

    // [20..23] cid (Little-Endian)
    frame.extend_from_slice(&cid.to_le_bytes());

    // [24..] payload
    if !payload.is_empty() {
        frame.extend_from_slice(payload);
    }

    frame
}

/// Parses the 24-byte TCP header from an incoming frame.
pub fn parse_tcp_frame(data: &[u8]) -> Option<ParsedTcpFrame> {
    if data.len() < 24 {
        return None;
    }

    let src_vip = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);
    let dst_vip = u32::from_be_bytes([data[8], data[9], data[10], data[11]]);
    let sport = u16::from_be_bytes([data[12], data[13]]);
    let dport = u16::from_be_bytes([data[14], data[15]]);
    let op = data[16];
    let ttl = data[17];
    let cid = i32::from_le_bytes([data[20], data[21], data[22], data[23]]);

    Some(ParsedTcpFrame {
        src_vip,
        dst_vip,
        sport,
        dport,
        op,
        ttl,
        cid,
        payload_offset: 24,
        payload_len: data.len() - 24,
    })
}

/// Rewrites occurrences of "10.77.0.1" to "127.0.0.1" in place.
/// Both patterns are exactly 9 ASCII bytes. Returns the count of replaced occurrences.
pub fn rewrite_server_addresses(bytes: &mut [u8]) -> usize {
    if bytes.len() < TARGET_IP_BYTES.len() {
        return 0;
    }

    let mut count = 0;
    let target_len = TARGET_IP_BYTES.len();
    let limit = bytes.len() - target_len;
    let mut i = 0;

    while i <= limit {
        if &bytes[i..i + target_len] == TARGET_IP_BYTES {
            bytes[i..i + target_len].copy_from_slice(REPLACEMENT_IP_BYTES);
            count += 1;
            i += target_len;
        } else {
            i += 1;
        }
    }

    count
}

// ---------------------------------------------------------------------------
// JNI Export Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_LocalOnlineBridge_nativeBuildTcpFrame(
    env: JNIEnv,
    _class: JClass,
    src_vip: jint,
    dst_vip: jint,
    sport: jint,
    dport: jint,
    op: jint,
    ttl: jint,
    cid: jint,
    payload: JByteArray,
) -> jbyteArray {
    let payload_vec = if !payload.is_null() {
        match env.convert_byte_array(&payload) {
            Ok(v) => v,
            Err(_) => Vec::new(),
        }
    } else {
        Vec::new()
    };

    let frame = encode_tcp_frame(
        src_vip as u32,
        dst_vip as u32,
        sport as u16,
        dport as u16,
        op as u8,
        ttl as u8,
        cid,
        &payload_vec,
    );

    match env.byte_array_from_slice(&frame) {
        Ok(arr) => arr.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_LocalOnlineBridge_nativeRewriteServerAddresses(
    mut env: JNIEnv,
    _class: JClass,
    data: JByteArray,
) -> jint {
    if data.is_null() {
        return 0;
    }

    let mut vec = match env.convert_byte_array(&data) {
        Ok(v) => v,
        Err(_) => return 0,
    };

    let count = rewrite_server_addresses(&mut vec);
    if count > 0 {
        let _ = env.set_byte_array_region(&data, 0, bytemuck_cast(&vec));
    }

    count as jint
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_LocalOnlineBridge_nativeParseTcpHeader(
    mut env: JNIEnv,
    _class: JClass,
    data: JByteArray,
    out_info: JIntArray, // [sport, dport, op, cid, payload_len]
) -> jint {
    if data.is_null() || out_info.is_null() {
        return 0;
    }

    let vec = match env.convert_byte_array(&data) {
        Ok(v) => v,
        Err(_) => return 0,
    };

    if let Some(parsed) = parse_tcp_frame(&vec) {
        let info = [
            parsed.sport as i32,
            parsed.dport as i32,
            parsed.op as i32,
            parsed.cid,
            parsed.payload_len as i32,
        ];
        let _ = env.set_int_array_region(&out_info, 0, &info);
        1
    } else {
        0
    }
}

fn bytemuck_cast(u: &[u8]) -> &[i8] {
    // Safe transmute of u8 slice to i8 slice for JNI set_byte_array_region
    unsafe { std::slice::from_raw_parts(u.as_ptr() as *const i8, u.len()) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_and_parse_tcp_frame() {
        let payload = b"Hello DirtySock";
        let frame = encode_tcp_frame(
            0x7F000001,
            VHOST_VIP,
            20920,
            45000,
            TCP_OP_DATA,
            64,
            1234,
            payload,
        );

        assert_eq!(frame.len(), 24 + payload.len());
        assert_eq!(frame[0], KIND_TCP);

        let parsed = parse_tcp_frame(&frame).expect("should parse");
        assert_eq!(parsed.src_vip, 0x7F000001);
        assert_eq!(parsed.dst_vip, VHOST_VIP);
        assert_eq!(parsed.sport, 20920);
        assert_eq!(parsed.dport, 45000);
        assert_eq!(parsed.op, TCP_OP_DATA);
        assert_eq!(parsed.ttl, 64);
        assert_eq!(parsed.cid, 1234);
        assert_eq!(parsed.payload_len, payload.len());
    }

    #[test]
    fn test_rewrite_server_addresses() {
        let mut msg = b"CONNECT 10.77.0.1:20920 HTTP/1.1\r\nHost: 10.77.0.1\r\n\r\n".to_vec();
        let replaced = rewrite_server_addresses(&mut msg);
        assert_eq!(replaced, 2);
        assert_eq!(
            &msg[..],
            b"CONNECT 127.0.0.1:20920 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
        );
    }
}
