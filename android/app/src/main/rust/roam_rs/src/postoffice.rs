//! Pure Rust implementation of the Prometheus Adhoc and Postoffice protocol engine.
//! Replaces Java AdhocPostofficeClient.java packet framing with zero-copy, bounds-checked memory safety.

pub const OPCODE_PING: u8 = 0;
pub const OPCODE_LOGIN: u8 = 1;
pub const OPCODE_CONNECT: u8 = 2;
pub const OPCODE_DISCONNECT: u8 = 3;
pub const OPCODE_CONNECT_BSSID: u8 = 6;
pub const OPCODE_CHAT: u8 = 7;

pub const AEMU_POSTOFFICE_INIT_PDP: u32 = 0;
pub const DEFAULT_GAME_PORT: u16 = 47210;
pub const LOCAL_RELAY_PORT: u16 = 47211;
pub const PRODUCT_CODE: &str = "NFSU2REVV";

/// Parsed adhocctl peer connect message.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AdhocConnectMsg<'a> {
    pub name: &'a str,
    pub mac: [u8; 6],
    pub ip: u32,
}

/// Parsed adhocctl chat message.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AdhocChatMsg<'a> {
    pub sender: &'a str,
    pub message: &'a str,
}

/// Builds an adhocctl LOGIN packet (opcode 1).
pub fn build_adhoc_login(name: &str, mac: &[u8; 6], out: &mut [u8]) -> Result<usize, &'static str> {
    // 1 byte opcode + 128 bytes name + 6 bytes MAC
    const TOTAL: usize = 1 + 128 + 6;
    if out.len() < TOTAL {
        return Err("Buffer too small for login packet");
    }
    out[0] = OPCODE_LOGIN;
    out[1..129].fill(0);
    let name_bytes = name.as_bytes();
    let copy_len = name_bytes.len().min(127);
    out[1..1 + copy_len].copy_from_slice(&name_bytes[..copy_len]);
    out[129..135].copy_from_slice(mac);
    Ok(TOTAL)
}

/// Builds an adhocctl CONNECT_BSSID packet (opcode 6).
pub fn build_adhoc_connect_bssid(bssid: &[u8; 6], out: &mut [u8]) -> Result<usize, &'static str> {
    const TOTAL: usize = 1 + 6;
    if out.len() < TOTAL {
        return Err("Buffer too small for bssid connect packet");
    }
    out[0] = OPCODE_CONNECT_BSSID;
    out[1..7].copy_from_slice(bssid);
    Ok(TOTAL)
}

/// Builds an adhocctl CHAT packet (opcode 7).
pub fn build_adhoc_chat(message: &str, out: &mut [u8]) -> Result<usize, &'static str> {
    const TOTAL: usize = 1 + 64;
    if out.len() < TOTAL {
        return Err("Buffer too small for chat packet");
    }
    out[0] = OPCODE_CHAT;
    out[1..65].fill(0);
    let msg_bytes = message.as_bytes();
    let copy_len = msg_bytes.len().min(63);
    out[1..1 + copy_len].copy_from_slice(&msg_bytes[..copy_len]);
    Ok(TOTAL)
}

/// Builds the 24-byte Postoffice INIT_PDP packet.
pub fn build_postoffice_init_pdp(local_mac: &[u8; 6], port: u16, out: &mut [u8]) -> Result<usize, &'static str> {
    const TOTAL: usize = 24;
    if out.len() < TOTAL {
        return Err("Buffer too small for postoffice init packet");
    }
    out.fill(0);
    // [0..4] = AEMU_POSTOFFICE_INIT_PDP (0) in little-endian
    out[0..4].copy_from_slice(&AEMU_POSTOFFICE_INIT_PDP.to_le_bytes());
    // [4..10] = MAC
    out[4..10].copy_from_slice(local_mac);
    // [10..12] = 0
    // [12..14] = port in little-endian
    let target_port = if port != 0 { port } else { DEFAULT_GAME_PORT };
    out[12..14].copy_from_slice(&target_port.to_le_bytes());
    // [14..22] = 8 zero bytes
    // [22..24] = 0
    Ok(TOTAL)
}

/// Parses the 14-byte Postoffice datagram header.
/// Returns (src_mac, src_port, payload_size).
pub fn parse_postoffice_dgram_header(header: &[u8]) -> Option<([u8; 6], u16, usize)> {
    if header.len() < 14 {
        return None;
    }
    let mut mac = [0u8; 6];
    mac.copy_from_slice(&header[0..6]);
    let src_port = u16::from_le_bytes([header[8], header[9]]);
    let size = u32::from_le_bytes([header[10], header[11], header[12], header[13]]) as usize;
    if size == 0 || size > 65535 {
        return None;
    }
    Some((mac, src_port, size))
}

/// Builds the 14-byte Postoffice datagram header for outgoing packets.
pub fn build_postoffice_dgram_header(
    dst_mac: &[u8; 6],
    dst_port: u16,
    payload_len: u32,
    out: &mut [u8],
) -> Result<usize, &'static str> {
    const TOTAL: usize = 14;
    if out.len() < TOTAL {
        return Err("Buffer too small for postoffice datagram header");
    }
    out.fill(0);
    out[0..6].copy_from_slice(dst_mac);
    // [6..8] = 0
    let target_port = if dst_port != 0 { dst_port } else { DEFAULT_GAME_PORT };
    out[8..10].copy_from_slice(&target_port.to_le_bytes());
    out[10..14].copy_from_slice(&payload_len.to_le_bytes());
    Ok(TOTAL)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_login_packet() {
        let mut buf = [0u8; 200];
        let mac = [0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC];
        let len = build_adhoc_login("Player1", &mac, &mut buf).unwrap();
        assert_eq!(len, 135);
        assert_eq!(buf[0], OPCODE_LOGIN);
        assert_eq!(&buf[1..8], b"Player1");
        assert_eq!(buf[8], 0);
        assert_eq!(&buf[129..135], &mac);
    }

    #[test]
    fn test_postoffice_init() {
        let mut buf = [0u8; 32];
        let mac = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
        let len = build_postoffice_init_pdp(&mac, 47210, &mut buf).unwrap();
        assert_eq!(len, 24);
        assert_eq!(&buf[4..10], &mac);
        let port = u16::from_le_bytes([buf[12], buf[13]]);
        assert_eq!(port, 47210);
    }

    #[test]
    fn test_postoffice_dgram_header() {
        let mut buf = [0u8; 14];
        let mac = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06];
        let len = build_postoffice_dgram_header(&mac, 47210, 1024, &mut buf).unwrap();
        assert_eq!(len, 14);

        let (parsed_mac, parsed_port, parsed_len) = parse_postoffice_dgram_header(&buf).unwrap();
        assert_eq!(parsed_mac, mac);
        assert_eq!(parsed_port, 47210);
        assert_eq!(parsed_len, 1024);
    }
}
