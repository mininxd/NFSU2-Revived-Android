//! Pure Rust implementation of the NFSU2 /lan Virtual Switch packet engine.
//! Replaces Java VirtualSwitchClient.java packet decapsulation, framing, and bundling
//! with zero-copy, bounds-checked memory safety.

pub const KIND_UDP: u8 = 1;
pub const KIND_LAN_UDP: u8 = 3;
pub const KIND_TCP: u8 = 4;
pub const KIND_LAN_ICMP: u8 = 5;
pub const KIND_BUNDLE: u8 = 32;

pub const DEFAULT_GAME_PORT: u16 = 47210;

/// Parsed Free Roam peer UDP datagram.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UdpPacketView<'a> {
    pub src_vip: u32,
    pub sport: u16,
    pub payload: &'a [u8],
}

/// Parsed virtual LAN TCP host frame.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TcpPacketView<'a> {
    pub src_vip: u32,
    pub dst_vip: u32,
    pub sport: u16,
    pub dport: u16,
    pub op: u8,
    pub ttl: u8,
    pub cid: u32,
    pub payload: &'a [u8],
}

/// Parsed incoming switch frame.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SwitchFrame<'a> {
    Udp(UdpPacketView<'a>),
    LanUdp(UdpPacketView<'a>),
    Tcp(TcpPacketView<'a>),
    Bundle(BundleIterator<'a>),
    Raw(u8, &'a [u8]),
}

/// Parses a KIND_UDP (1) packet frame with zero copy.
pub fn parse_udp_frame(frame: &[u8]) -> Option<UdpPacketView<'_>> {
    if frame.len() < 16 {
        return None;
    }
    let src_vip = u32::from_be_bytes([frame[4], frame[5], frame[6], frame[7]]);
    let mut sport = u16::from_be_bytes([frame[12], frame[13]]);
    if sport == 0 {
        sport = DEFAULT_GAME_PORT;
    }
    if sport == 9999 {
        return None; // Ping/keepalive port ignored
    }
    Some(UdpPacketView {
        src_vip,
        sport,
        payload: &frame[16..],
    })
}

/// Parses a KIND_LAN_UDP (3) packet frame with zero copy.
pub fn parse_lan_udp_frame(frame: &[u8]) -> Option<UdpPacketView<'_>> {
    if frame.len() < 24 {
        return None;
    }
    let src_vip = u32::from_be_bytes([frame[4], frame[5], frame[6], frame[7]]);
    let mut sport = u16::from_be_bytes([frame[12], frame[13]]);
    if sport == 0 {
        sport = DEFAULT_GAME_PORT;
    }
    if sport == 9999 {
        return None;
    }
    Some(UdpPacketView {
        src_vip,
        sport,
        payload: &frame[24..],
    })
}

/// Parses a KIND_TCP (4) packet frame with zero copy.
pub fn parse_tcp_frame(frame: &[u8]) -> Option<TcpPacketView<'_>> {
    if frame.len() < 24 {
        return None;
    }
    let src_vip = u32::from_be_bytes([frame[4], frame[5], frame[6], frame[7]]);
    let dst_vip = u32::from_be_bytes([frame[8], frame[9], frame[10], frame[11]]);
    let sport = u16::from_be_bytes([frame[12], frame[13]]);
    let dport = u16::from_be_bytes([frame[14], frame[15]]);
    let op = frame[16];
    let ttl = frame[17];
    let cid = u32::from_le_bytes([frame[20], frame[21], frame[22], frame[23]]);

    Some(TcpPacketView {
        src_vip,
        dst_vip,
        sport,
        dport,
        op,
        ttl,
        cid,
        payload: &frame[24..],
    })
}

/// Zero-copy iterator over bundled frames in a KIND_BUNDLE (32) container.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BundleIterator<'a> {
    data: &'a [u8],
    at: usize,
}

impl<'a> BundleIterator<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data, at: 4 }
    }
}

impl<'a> Iterator for BundleIterator<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        while self.at + 4 <= self.data.len() {
            let k = self.data[self.at] as usize;
            let flen = ((self.data[self.at + 2] as usize) << 8) | (self.data[self.at + 3] as usize);
            let f = self.at + 4 + 4 * k;
            let end = f + flen;

            if flen >= 16 && end <= self.data.len() {
                self.at = end;
                return Some(&self.data[f..end]);
            }
            // Move forward to prevent infinite loops on malformed lengths
            self.at = if end > self.at { end } else { self.data.len() };
        }
        None
    }
}

/// Demultiplexes any incoming binary message from the switch.
pub fn parse_switch_message(data: &[u8]) -> Option<SwitchFrame<'_>> {
    if data.is_empty() {
        return None;
    }
    match data[0] {
        KIND_UDP => parse_udp_frame(data).map(SwitchFrame::Udp),
        KIND_LAN_UDP => parse_lan_udp_frame(data).map(SwitchFrame::LanUdp),
        KIND_TCP => parse_tcp_frame(data).map(SwitchFrame::Tcp),
        KIND_BUNDLE => Some(SwitchFrame::Bundle(BundleIterator::new(data))),
        other => Some(SwitchFrame::Raw(other, &data[1..])),
    }
}

/// Builds an outgoing KIND_UDP (1) datagram frame into `out`. Returns the number of bytes written.
pub fn build_udp_frame(
    src_vip: u32,
    dst_vip: u32,
    dport: u16,
    payload: &[u8],
    out: &mut [u8],
) -> Result<usize, &'static str> {
    let total_len = 16 + payload.len();
    if out.len() < total_len {
        return Err("Destination buffer too small for UDP frame");
    }

    out[0] = KIND_UDP;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;

    out[4..8].copy_from_slice(&src_vip.to_be_bytes());
    out[8..12].copy_from_slice(&dst_vip.to_be_bytes());
    out[12..14].copy_from_slice(&DEFAULT_GAME_PORT.to_be_bytes());
    let dest_port = if dport != 0 { dport } else { DEFAULT_GAME_PORT };
    out[14..16].copy_from_slice(&dest_port.to_be_bytes());

    out[16..total_len].copy_from_slice(payload);
    Ok(total_len)
}

/// Builds an outgoing KIND_TCP (4) frame into `out`. Returns the number of bytes written.
#[allow(clippy::too_many_arguments)]
pub fn build_tcp_frame(
    src_vip: u32,
    dst_vip: u32,
    sport: u16,
    dport: u16,
    op: u8,
    ttl: u8,
    cid: u32,
    payload: &[u8],
    out: &mut [u8],
) -> Result<usize, &'static str> {
    let total_len = 24 + payload.len();
    if out.len() < total_len {
        return Err("Destination buffer too small for TCP frame");
    }

    out[0] = KIND_TCP;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;

    out[4..8].copy_from_slice(&src_vip.to_be_bytes());
    out[8..12].copy_from_slice(&dst_vip.to_be_bytes());
    out[12..14].copy_from_slice(&sport.to_be_bytes());
    out[14..16].copy_from_slice(&dport.to_be_bytes());
    out[16] = op;
    out[17] = ttl;
    out[18] = 0;
    out[19] = 0;
    out[20..24].copy_from_slice(&cid.to_le_bytes());

    out[24..total_len].copy_from_slice(payload);
    Ok(total_len)
}

/// Builds an RFC 6455 client WebSocket frame with masking. Returns the number of bytes written.
pub fn build_ws_frame(
    opcode: u8,
    payload: &[u8],
    mask: Option<[u8; 4]>,
    out: &mut [u8],
) -> Result<usize, &'static str> {
    let mask_flag = if mask.is_some() { 0x80u8 } else { 0x00u8 };
    let len = payload.len();

    let mut header_len = 2;
    if len > 65535 {
        header_len += 8;
    } else if len > 125 {
        header_len += 2;
    }
    if mask.is_some() {
        header_len += 4;
    }

    let total = header_len + len;
    if out.len() < total {
        return Err("Buffer too small for WebSocket frame");
    }

    out[0] = 0x80 | (opcode & 0x0F); // FIN + opcode

    let mut at = 1;
    if len <= 125 {
        out[at] = mask_flag | (len as u8);
        at += 1;
    } else if len <= 65535 {
        out[at] = mask_flag | 126;
        out[at + 1] = ((len >> 8) & 0xFF) as u8;
        out[at + 2] = (len & 0xFF) as u8;
        at += 3;
    } else {
        out[at] = mask_flag | 127;
        let be = (len as u64).to_be_bytes();
        out[at + 1..at + 9].copy_from_slice(&be);
        at += 9;
    }

    if let Some(m) = mask {
        out[at..at + 4].copy_from_slice(&m);
        at += 4;
        for i in 0..len {
            out[at + i] = payload[i] ^ m[i % 4];
        }
    } else {
        out[at..at + len].copy_from_slice(payload);
    }

    Ok(total)
}

// ---- C ABI Exports for Native Integration ---------------------------------

#[no_mangle]
pub unsafe extern "C" fn roam_switch_build_udp(
    src_vip: u32,
    dst_vip: u32,
    dport: u16,
    payload_ptr: *const u8,
    payload_len: usize,
    out_ptr: *mut u8,
    out_cap: usize,
) -> i32 {
    if payload_ptr.is_null() || out_ptr.is_null() {
        return -1;
    }
    let payload = std::slice::from_raw_parts(payload_ptr, payload_len);
    let out = std::slice::from_raw_parts_mut(out_ptr, out_cap);
    match build_udp_frame(src_vip, dst_vip, dport, payload, out) {
        Ok(written) => written as i32,
        Err(_) => -2,
    }
}

#[no_mangle]
pub unsafe extern "C" fn roam_switch_parse_udp(
    frame_ptr: *const u8,
    frame_len: usize,
    out_src_vip: *mut u32,
    out_sport: *mut u16,
    out_payload_offset: *mut usize,
    out_payload_len: *mut usize,
) -> i32 {
    if frame_ptr.is_null() || out_src_vip.is_null() || out_sport.is_null() {
        return -1;
    }
    let frame = std::slice::from_raw_parts(frame_ptr, frame_len);
    if let Some(view) = parse_udp_frame(frame) {
        *out_src_vip = view.src_vip;
        *out_sport = view.sport;
        if !out_payload_offset.is_null() {
            *out_payload_offset = 16;
        }
        if !out_payload_len.is_null() {
            *out_payload_len = view.payload.len();
        }
        0
    } else {
        -1
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_udp_roundtrip() {
        let mut buf = [0u8; 64];
        let payload = b"SPEED2_ROAM_PING";
        let written = build_udp_frame(0x0A000001, 0x0A000002, 47210, payload, &mut buf).unwrap();
        assert_eq!(written, 16 + payload.len());

        let parsed = parse_udp_frame(&buf[..written]).expect("parse_udp_frame");
        assert_eq!(parsed.src_vip, 0x0A000001);
        assert_eq!(parsed.sport, 47210);
        assert_eq!(parsed.payload, payload);
    }

    #[test]
    fn test_tcp_roundtrip() {
        let mut buf = [0u8; 128];
        let payload = b"TCP_DATA_STREAM";
        let written = build_tcp_frame(
            0x0A000001,
            0x0A000002,
            1234,
            80,
            1,
            64,
            42,
            payload,
            &mut buf,
        )
        .unwrap();
        assert_eq!(written, 24 + payload.len());

        let parsed = parse_tcp_frame(&buf[..written]).expect("parse_tcp_frame");
        assert_eq!(parsed.src_vip, 0x0A000001);
        assert_eq!(parsed.dst_vip, 0x0A000002);
        assert_eq!(parsed.sport, 1234);
        assert_eq!(parsed.dport, 80);
        assert_eq!(parsed.op, 1);
        assert_eq!(parsed.ttl, 64);
        assert_eq!(parsed.cid, 42u32);
        assert_eq!(parsed.payload, payload);
    }

    #[test]
    fn test_ws_framing() {
        let mut buf = [0u8; 64];
        let payload = b"hello";
        let mask = Some([0x12, 0x34, 0x56, 0x78]);
        let written = build_ws_frame(1, payload, mask, &mut buf).unwrap();
        assert_eq!(written, 2 + 4 + payload.len());
        assert_eq!(buf[0], 0x81); // FIN + text
        assert_eq!(buf[1] & 0x80, 0x80); // Masked
        assert_eq!(buf[1] & 0x7F, 5); // Length 5
    }

    #[test]
    fn test_bundle_iterator() {
        let mut bundle = vec![KIND_BUNDLE, 0, 0, 0];
        // Add one 16-byte UDP frame
        let mut f1 = [0u8; 20];
        f1[0] = KIND_UDP;
        f1[4] = 1;
        f1[12] = (DEFAULT_GAME_PORT >> 8) as u8;
        f1[13] = (DEFAULT_GAME_PORT & 0xFF) as u8;
        // header: k=0, flen=20
        bundle.extend_from_slice(&[0, 0, 0, 20]);
        bundle.extend_from_slice(&f1);

        let frames: Vec<&[u8]> = BundleIterator::new(&bundle).collect();
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].len(), 20);
        assert_eq!(frames[0][0], KIND_UDP);
    }
}
