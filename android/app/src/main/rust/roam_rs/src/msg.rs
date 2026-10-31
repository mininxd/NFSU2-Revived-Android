//! Wire message encoding and decoding for Free Roam multiplayer protocol.

use std::os::raw::{c_char, c_int};
use crate::crc::rc_crc32;
use crate::wire::{PacketReader, PacketWriter};

pub const RC_MAGIC0: u8 = 0x4E; // 'N'
pub const RC_MAGIC1: u8 = 0x46; // 'F'
pub const RC_MAGIC2: u8 = 0x53; // 'S'
pub const RC_MAGIC3: u8 = 0x55; // 'U'
pub const RC_PROTO_VER: u8 = 5;

pub const RC_MAX_DGRAM: usize = 1400;
pub const RC_HDR: usize = 12;
pub const RC_CRC: usize = 4;

pub const RC_NAME_MAX: usize = 24;
pub const RC_MAX_PEERS: usize = 32;
pub const RC_CREW_MAX: usize = 4;
pub const RC_SNAP_STATES: usize = 3;
pub const RC_CFG_MAX: usize = 2048;
pub const RC_CFG_FRAG: usize = 1000;
pub const RC_CFG_MAXFRAG: usize = 3;
pub const RC_LOBBY_MAX: usize = 6;
pub const RC_MAP_PARTS: usize = 3;
pub const RC_MAP_PER: usize = 230;
pub const RC_MAP_ENTRY: usize = 5;
pub const RC_LOBBIES_MAX: usize = 96;
pub const RC_LOBBIES_ENTRY: usize = 11;
pub const RC_RACEREP_NAME: usize = 16;
pub const RC_RACEREP_BYTES: usize = 60;

pub const RC_STATEX_V4: u32 = 10;
pub const RC_STATEX_HYD: u32 = 12;
pub const RC_STATEX_SLIP: u32 = 24;
pub const RC_STATEX_ENG: u32 = 5;
pub const RC_SLIP_STEP: f32 = 0.5;

pub const RC_HF_STATEX: u16 = 0x0001;
pub const RC_HF_CREW: u16 = 0x0002;

pub const RC_MSG_HELLO: u8 = 1;
pub const RC_MSG_PEERS: u8 = 2;
pub const RC_MSG_SNAP: u8 = 3;
pub const RC_MSG_CFG: u8 = 4;
pub const RC_MSG_CFG_REQ: u8 = 5;
pub const RC_MSG_PING: u8 = 6;
pub const RC_MSG_PONG: u8 = 7;
pub const RC_MSG_BYE: u8 = 8;
pub const RC_MSG_DUEL: u8 = 9;
pub const RC_MSG_MAPREQ: u8 = 10;
pub const RC_MSG_MAP: u8 = 11;
pub const RC_MSG_LOBBY: u8 = 12;
pub const RC_MSG_LOBBIES: u8 = 13;
pub const RC_MSG_RACEREP: u8 = 14;
pub const RC_MSG_SAY: u8 = 15;

pub const RC_DUEL__N: u8 = 6;
pub const RC_LOBBY__N: u8 = 6;
pub const RC_LS__N: u8 = 3;
pub const RC_LL__N: u8 = 5;
pub const RC_ST__N: u8 = 6;

pub const RC_RR_HOST: u8 = 0x1;
pub const RC_RR_QUIT: u8 = 0x2;

pub const RC_OK: c_int = 0;
pub const RC_E_SHORT: c_int = -1;
pub const RC_E_MAGIC: c_int = -2;
pub const RC_E_VERSION: c_int = -3;
pub const RC_E_TYPE: c_int = -4;
pub const RC_E_LENGTH: c_int = -5;
pub const RC_E_CRC: c_int = -6;
pub const RC_E_FIELD: c_int = -7;
pub const RC_E_RANGE: c_int = -8;
pub const RC_E_TRAIL: c_int = -9;

const ENG_MAX: u16 = 20000;

extern "C" {
    pub fn rc_name_blocked(name: *const c_char) -> c_int;
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct rc_state {
    pub t_us: u32,
    pub p: [f32; 3],
    pub v: [f32; 3],
    pub q: [f32; 4],
    pub w: [f32; 3],
    pub steer: f32,
    pub throttle: f32,
    pub brake: f32,
    pub rpm: f32,
    pub speed: f32,
    pub gear: i8,
    pub bits: u8,
    pub ext: u8,
    pub _pad0: u8,
    pub wz: [f32; 4],
    pub nos: u8,
    pub hyd: u8,
    pub thumps: u8,
    pub slipx: u8,
    pub slip: [f32; 8],
    pub surf: [u8; 4],
    pub eng: u16,
    pub eng_acc: u8,
    pub eng_dcl: u8,
    pub eng_base: u8,
    pub _pad1: [u8; 3],
}

impl Default for rc_state {
    fn default() -> Self {
        unsafe { std::mem::zeroed() }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct rc_hdr {
    pub ver: u8,
    pub r#type: u8,
    pub len: u16,
    pub sender: u32,
    pub seq: u16,
    pub flags: u16,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct rc_lobby {
    pub op: u8,
    pub arg: u8,
    pub state: u8,
    pub max: u8,
    pub to: u32,
    pub lobby: u32,
    pub race: u32,
    pub x: i16,
    pub y: i16,
    pub t_ms: u16,
    pub n: u8,
    pub _pad: u8,
    pub ids: [u32; RC_LOBBY_MAX],
}

impl Default for rc_lobby {
    fn default() -> Self {
        unsafe { std::mem::zeroed() }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct rc_racerep {
    pub lobby: u32,
    pub race: u32,
    pub mode: u8,
    pub laps: u8,
    pub track: u16,
    pub place: u8,
    pub players: u8,
    pub flags: u8,
    pub r#type: u8,
    pub cls: i8,
    pub _pad0: [u8; 3],
    pub rating: f32,
    pub field: f32,
    pub factor: f32,
    pub kw: f32,
    pub kg: f32,
    pub kmh: f32,
    pub time: f32,
    pub name: [c_char; RC_RACEREP_NAME + 1],
    pub _pad1: [u8; 3],
}

impl Default for rc_racerep {
    fn default() -> Self {
        unsafe { std::mem::zeroed() }
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_hello {
    pub core_ver: u16,
    pub platform: u8,
    pub status: u8,
    pub listen_port: u16,
    pub cfg_rev: u16,
    pub room: u32,
    pub name: [c_char; RC_NAME_MAX + 1],
    pub n_seen: u8,
    pub _pad0: [u8; 2],
    pub seen: [u32; RC_MAX_PEERS],
    pub n_crews: u8,
    pub _pad1: [u8; 3],
    pub crews: [u32; RC_CREW_MAX],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_peer_entry {
    pub ip: u32,
    pub port: u16,
    pub _pad: u16,
    pub id: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_peers {
    pub n: u8,
    pub _pad0: [u8; 3],
    pub e: [rc_peer_entry; RC_MAX_PEERS],
    pub n_mine: u8,
    pub _pad1: [u8; 3],
    pub mine: [u32; RC_CREW_MAX],
    pub crew: [u8; RC_MAX_PEERS],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_snap {
    pub status: u8,
    pub epoch: u8,
    pub cfg_rev: u16,
    pub n: u8,
    pub clock: u8,
    pub _pad: [u8; 2],
    pub s: [rc_state; RC_SNAP_STATES],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_cfg {
    pub cfg_rev: u16,
    pub frag: u8,
    pub nfrag: u8,
    pub total: u16,
    pub dlen: u16,
    pub data: *const u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_cfgreq {
    pub cfg_rev: u16,
    pub have_mask: u8,
    pub _pad: u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_ping {
    pub t0: u32,
    pub t1: u32,
    pub t2: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_bye {
    pub reason: u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_duel {
    pub op: u8,
    pub arg: u8,
    pub _pad: [u8; 2],
    pub to: u32,
    pub duel: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_mapreq {
    pub req: u16,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_map {
    pub req: u16,
    pub total: u16,
    pub part: u8,
    pub nparts: u8,
    pub n: u8,
    pub _pad: u8,
    pub data: *const u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_lobbies {
    pub req: u16,
    pub n: u8,
    pub _pad: u8,
    pub data: *const u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg_say {
    pub phrase: u8,
    pub seq: u8,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union rc_msg_union {
    pub hello: rc_msg_hello,
    pub peers: rc_msg_peers,
    pub snap: rc_msg_snap,
    pub cfg: rc_msg_cfg,
    pub cfgreq: rc_msg_cfgreq,
    pub ping: rc_msg_ping,
    pub bye: rc_msg_bye,
    pub duel: rc_msg_duel,
    pub mapreq: rc_msg_mapreq,
    pub map: rc_msg_map,
    pub lobby: rc_lobby,
    pub lobbies: rc_msg_lobbies,
    pub racerep: rc_racerep,
    pub say: rc_msg_say,
    pub raw: [u8; 440],
}

impl Default for rc_msg_union {
    fn default() -> Self {
        rc_msg_union { raw: [0u8; 440] }
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct rc_msg {
    pub h: rc_hdr,
    pub _pad: [u8; 4],
    pub u: rc_msg_union,
}

impl Default for rc_msg {
    fn default() -> Self {
        rc_msg {
            h: rc_hdr::default(),
            _pad: [0u8; 4],
            u: rc_msg_union::default(),
        }
    }
}

#[inline]
fn f_clamp(val: f32, min_val: f32, max_val: f32) -> f32 {
    if val < min_val { min_val } else if val > max_val { max_val } else { val }
}

#[inline]
fn f_round(val: f32) -> i32 {
    if val >= 0.0 {
        (val + 0.5) as i32
    } else {
        (val - 0.5) as i32
    }
}

#[inline]
fn f_finite(val: f32) -> bool {
    val.is_finite()
}

#[inline]
fn f_abs(val: f32) -> f32 {
    val.abs()
}

#[inline]
fn q16(f: f32, scale: f32) -> i16 {
    let v = f_round(f * scale);
    if v > 32767 { 32767 } else if v < -32767 { -32767 } else { v as i16 }
}

#[inline]
fn q_slip(f: f32) -> i8 {
    let v = f_round(f_clamp(f, -1000.0, 1000.0) / RC_SLIP_STEP);
    if v > 127 { 127 } else if v < -127 { -127 } else { v as i8 }
}

#[inline]
fn q_norm(q: &mut [f32; 4]) {
    let len_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if len_sq > 1e-8 {
        let inv = 1.0 / len_sq.sqrt();
        q[0] *= inv;
        q[1] *= inv;
        q[2] *= inv;
        q[3] *= inv;
    } else {
        q[0] = 0.0;
        q[1] = 0.0;
        q[2] = 0.0;
        q[3] = 1.0;
    }
}

fn put_state(w: &mut PacketWriter, s: &rc_state) {
    w.put_u32(s.t_us);
    for &p in &s.p { w.put_f32(p); }
    for &v in &s.v { w.put_f32(v); }
    let mut q = s.q;
    q_norm(&mut q);
    for &val in &q { w.put_u16(q16(val, 32767.0) as u16); }
    for &val in &s.w { w.put_u16(q16(val, 1000.0) as u16); }
    w.put_u16(q16(f_clamp(s.steer, -1.0, 1.0), 32767.0) as u16);
    w.put_u8(f_round(f_clamp(s.throttle, 0.0, 1.0) * 255.0) as u8);
    w.put_u8(f_round(f_clamp(s.brake, 0.0, 1.0) * 255.0) as u8);
    w.put_u16(f_round(f_clamp(s.rpm, 0.0, 65535.0)) as u16);
    w.put_u16(q16(f_clamp(s.speed, -327.0, 327.0), 100.0) as u16);
    w.put_u8(s.gear as u8);
    w.put_u8(s.bits);
}

fn put_statex(w: &mut PacketWriter, s: &rc_state) {
    if s.ext == 0 {
        w.put_u8(0);
        return;
    }
    let mut slides = false;
    for &slip in &s.slip {
        if q_slip(slip) != 0 {
            slides = true;
            break;
        }
    }
    let eng = s.eng != 0;
    let base_len = if slides { RC_STATEX_SLIP } else { RC_STATEX_HYD };
    let total_len = base_len + if eng { RC_STATEX_ENG } else { 0 } - 1;
    w.put_u8(total_len as u8);
    for &wz in &s.wz {
        w.put_u16(q16(f_clamp(wz, -32.0, 32.0), 1000.0) as u16);
    }
    w.put_u8(s.nos);
    w.put_u8(s.hyd);
    w.put_u8(s.thumps);
    if slides {
        for i in 0..4 {
            w.put_u8(q_slip(s.slip[2 * i]) as u8);
            w.put_u8(q_slip(s.slip[2 * i + 1]) as u8);
            w.put_u8(s.surf[i]);
        }
    }
    if eng {
        w.put_u16(if s.eng < ENG_MAX { s.eng } else { ENG_MAX });
        w.put_u8(s.eng_acc);
        w.put_u8(s.eng_dcl);
        w.put_u8(s.eng_base);
    }
}

fn get_state(r: &mut PacketReader, s: &mut rc_state) -> c_int {
    *s = rc_state::default();
    s.t_us = r.get_u32();
    for p in &mut s.p { *p = r.get_f32(); }
    for v in &mut s.v { *v = r.get_f32(); }
    for q in &mut s.q { *q = (r.get_u16() as i16 as f32) / 32767.0; }
    for w in &mut s.w { *w = (r.get_u16() as i16 as f32) / 1000.0; }
    s.steer = (r.get_u16() as i16 as f32) / 32767.0;
    s.throttle = (r.get_u8() as f32) / 255.0;
    s.brake = (r.get_u8() as f32) / 255.0;
    s.rpm = r.get_u16() as f32;
    s.speed = (r.get_u16() as i16 as f32) / 100.0;
    s.gear = r.get_u8() as i8;
    s.bits = r.get_u8();

    if r.has_error() {
        return RC_E_SHORT;
    }
    for &p in &s.p { if !f_finite(p) { return RC_E_RANGE; } }
    for &v in &s.v { if !f_finite(v) { return RC_E_RANGE; } }
    if f_abs(s.p[0]) > 20000.0 || f_abs(s.p[1]) > 20000.0 || f_abs(s.p[2]) > 2000.0 {
        return RC_E_RANGE;
    }
    if s.v[0] * s.v[0] + s.v[1] * s.v[1] + s.v[2] * s.v[2] > 200.0 * 200.0 {
        return RC_E_RANGE;
    }
    let n = s.q[0] * s.q[0] + s.q[1] * s.q[1] + s.q[2] * s.q[2] + s.q[3] * s.q[3];
    if n < 0.25 {
        return RC_E_RANGE;
    }
    q_norm(&mut s.q);
    RC_OK
}

fn get_statex(r: &mut PacketReader, s: &mut rc_state) -> c_int {
    let n = r.get_u8() as usize;
    if r.has_error() {
        return RC_E_SHORT;
    }
    if n == 0 {
        return RC_OK;
    }
    if n < (RC_STATEX_V4 as usize - 1) {
        return RC_E_FIELD;
    }
    if r.remaining() < n {
        return RC_E_SHORT;
    }
    for wz in &mut s.wz {
        *wz = (r.get_u16() as i16 as f32) / 1000.0;
    }
    s.nos = r.get_u8();
    let mut k = RC_STATEX_V4 as usize - 1;
    if n >= (RC_STATEX_HYD as usize - 1) {
        s.hyd = r.get_u8();
        s.thumps = r.get_u8();
        k = RC_STATEX_HYD as usize - 1;
    }
    if n >= (RC_STATEX_SLIP as usize - 1) {
        for i in 0..4 {
            s.slip[2 * i] = (r.get_u8() as i8 as f32) * RC_SLIP_STEP;
            s.slip[2 * i + 1] = (r.get_u8() as i8 as f32) * RC_SLIP_STEP;
            s.surf[i] = r.get_u8();
        }
        s.slipx = 1;
        k = RC_STATEX_SLIP as usize - 1;
    }
    if n >= k + (RC_STATEX_ENG as usize) {
        let e = r.get_u16();
        s.eng = if e < ENG_MAX { e } else { ENG_MAX };
        s.eng_acc = r.get_u8();
        s.eng_dcl = r.get_u8();
        s.eng_base = r.get_u8();
        k += RC_STATEX_ENG as usize;
    }
    let extra = n - k;
    if extra > 0 {
        r.skip(extra);
    }
    s.ext = 1;
    RC_OK
}

#[inline]
fn is_printable(bytes: &[u8]) -> bool {
    bytes.iter().all(|&b| b >= 0x20 && b < 0x7F)
}

fn lobby_ok(l: &rc_lobby) -> bool {
    if l.op < 1 || l.op >= RC_LOBBY__N || l.state >= RC_LS__N || l.arg >= RC_LL__N || l.lobby == 0 || l.race == 0 {
        return false;
    }
    if l.max < 2 || l.max > (RC_LOBBY_MAX as u8) || l.n > l.max {
        return false;
    }
    for i in 0..(l.n as usize) {
        if l.ids[i] == 0 { return false; }
        for j in 0..i {
            if l.ids[j] == l.ids[i] { return false; }
        }
    }
    true
}

#[inline]
fn in_range(f: f32, lo: f32, hi: f32) -> bool {
    f_finite(f) && f >= lo && f <= hi
}

fn racerep_ok(r: &rc_racerep) -> bool {
    if r.lobby == 0 || r.race == 0 || r.players < 1 || r.players > (RC_LOBBY_MAX as u8) || r.place > r.players {
        return false;
    }
    if r.cls < -1 || r.cls > 3 || (r.flags & !(RC_RR_HOST | RC_RR_QUIT)) != 0 {
        return false;
    }
    if !in_range(r.rating, 0.0, 5000.0) || !in_range(r.field, 0.0, 5000.0) || !in_range(r.factor, 0.01, 100.0)
        || !in_range(r.kw, 0.0, 10000.0) || !in_range(r.kg, 0.0, 50000.0) || !in_range(r.kmh, 0.0, 2000.0)
        || !in_range(r.time, 0.0, 100000.0) {
        return false;
    }
    let mut n = 0;
    while n < RC_RACEREP_NAME && r.name[n] != 0 { n += 1; }
    let slice = unsafe { std::slice::from_raw_parts(r.name.as_ptr() as *const u8, n) };
    is_printable(slice)
}

/// Encodes one datagram into `out`. Returns length or -1 on error.
#[no_mangle]
pub unsafe extern "C" fn rc_encode(m: *const rc_msg, out: *mut u8, cap: c_int) -> c_int {
    if m.is_null() || out.is_null() || cap < (RC_HDR + RC_CRC) as c_int {
        return -1;
    }
    let m = &*m;
    let max_len = if (cap as usize) < RC_MAX_DGRAM { cap as usize } else { RC_MAX_DGRAM };
    let out_slice = std::slice::from_raw_parts_mut(out, max_len);
    let mut w = PacketWriter::new(out_slice);

    w.put_u8(RC_MAGIC0);
    w.put_u8(RC_MAGIC1);
    w.put_u8(RC_MAGIC2);
    w.put_u8(RC_MAGIC3);
    w.put_u8(RC_PROTO_VER);
    w.put_u8(m.h.r#type);
    w.put_u16(0); // length placeholder
    w.put_u32(m.h.sender);
    w.put_u16(m.h.seq);
    w.put_u16(m.h.flags);

    match m.h.r#type {
        RC_MSG_HELLO => {
            let hello = &m.u.hello;
            let mut nl = 0;
            while nl < RC_NAME_MAX && hello.name[nl] != 0 { nl += 1; }
            if hello.n_seen as usize > RC_MAX_PEERS { return -1; }
            w.put_u16(hello.core_ver);
            w.put_u8(hello.platform);
            w.put_u8(hello.status);
            w.put_u16(hello.listen_port);
            w.put_u16(hello.cfg_rev);
            w.put_u32(hello.room);
            w.put_u8(nl as u8);
            let name_slice = std::slice::from_raw_parts(hello.name.as_ptr() as *const u8, nl);
            w.put_bytes(name_slice);
            w.put_u8(hello.n_seen);
            for i in 0..(hello.n_seen as usize) {
                w.put_u32(hello.seen[i]);
            }
            if hello.n_crews > 0 {
                if (m.h.flags & RC_HF_CREW) == 0 || hello.n_crews as usize > RC_CREW_MAX {
                    return -1;
                }
                w.put_u8(hello.n_crews);
                for i in 0..(hello.n_crews as usize) {
                    if hello.crews[i] == 0 { return -1; }
                    w.put_u32(hello.crews[i]);
                }
            }
        }
        RC_MSG_PEERS => {
            let peers = &m.u.peers;
            if (peers.n as usize) > RC_MAX_PEERS { return -1; }
            w.put_u8(peers.n);
            for i in 0..(peers.n as usize) {
                let ip = peers.e[i].ip;
                w.put_u8((ip >> 24) as u8);
                w.put_u8((ip >> 16) as u8);
                w.put_u8((ip >> 8) as u8);
                w.put_u8(ip as u8);
                w.put_u16(peers.e[i].port);
                w.put_u32(peers.e[i].id);
            }
            if (m.h.flags & RC_HF_CREW) != 0 {
                if peers.n_mine as usize > RC_CREW_MAX { return -1; }
                w.put_u8(peers.n_mine);
                for i in 0..(peers.n_mine as usize) {
                    if peers.mine[i] == 0 { return -1; }
                    w.put_u32(peers.mine[i]);
                }
                for i in 0..(peers.n as usize) {
                    w.put_u8(peers.crew[i]);
                }
            }
        }
        RC_MSG_SNAP => {
            let snap = &m.u.snap;
            if snap.n < 1 || (snap.n as usize) > RC_SNAP_STATES { return -1; }
            w.put_u8(snap.status);
            w.put_u8(snap.epoch);
            w.put_u16(snap.cfg_rev);
            w.put_u8(snap.n);
            w.put_u8(snap.clock);
            for i in 0..(snap.n as usize) {
                put_state(&mut w, &snap.s[i]);
                if (m.h.flags & RC_HF_STATEX) != 0 {
                    put_statex(&mut w, &snap.s[i]);
                }
            }
        }
        RC_MSG_CFG => {
            let cfg = &m.u.cfg;
            if cfg.nfrag < 1 || (cfg.nfrag as usize) > RC_CFG_MAXFRAG || cfg.frag >= cfg.nfrag
                || (cfg.total as usize) > RC_CFG_MAX || (cfg.dlen as usize) > RC_CFG_FRAG {
                return -1;
            }
            w.put_u16(cfg.cfg_rev);
            w.put_u8(cfg.frag);
            w.put_u8(cfg.nfrag);
            w.put_u16(cfg.total);
            w.put_u16(cfg.dlen);
            if cfg.dlen > 0 && !cfg.data.is_null() {
                let data_slice = std::slice::from_raw_parts(cfg.data, cfg.dlen as usize);
                w.put_bytes(data_slice);
            }
        }
        RC_MSG_CFG_REQ => {
            let req = &m.u.cfgreq;
            w.put_u16(req.cfg_rev);
            w.put_u8(req.have_mask);
        }
        RC_MSG_PING => {
            w.put_u32(m.u.ping.t0);
        }
        RC_MSG_PONG => {
            w.put_u32(m.u.ping.t0);
            w.put_u32(m.u.ping.t1);
            w.put_u32(m.u.ping.t2);
        }
        RC_MSG_BYE => {
            w.put_u8(m.u.bye.reason);
        }
        RC_MSG_DUEL => {
            let duel = &m.u.duel;
            if duel.op < 1 || duel.op >= RC_DUEL__N || duel.to == 0 || duel.duel == 0 {
                return -1;
            }
            w.put_u8(duel.op);
            w.put_u8(duel.arg);
            w.put_u32(duel.to);
            w.put_u32(duel.duel);
        }
        RC_MSG_MAPREQ => {
            w.put_u16(m.u.mapreq.req);
        }
        RC_MSG_MAP => {
            let map = &m.u.map;
            if map.nparts < 1 || (map.nparts as usize) > RC_MAP_PARTS || map.part >= map.nparts || (map.n as usize) > RC_MAP_PER {
                return -1;
            }
            w.put_u16(map.req);
            w.put_u8(map.part);
            w.put_u8(map.nparts);
            w.put_u16(map.total);
            w.put_u8(map.n);
            let len = (map.n as usize) * RC_MAP_ENTRY;
            if len > 0 && !map.data.is_null() {
                let data_slice = std::slice::from_raw_parts(map.data, len);
                w.put_bytes(data_slice);
            }
        }
        RC_MSG_LOBBY => {
            let lobby = &m.u.lobby;
            if !lobby_ok(lobby) { return -1; }
            w.put_u8(lobby.op);
            w.put_u8(lobby.arg);
            w.put_u8(lobby.state);
            w.put_u8(lobby.max);
            w.put_u32(lobby.to);
            w.put_u32(lobby.lobby);
            w.put_u32(lobby.race);
            w.put_u16(lobby.x as u16);
            w.put_u16(lobby.y as u16);
            w.put_u16(lobby.t_ms);
            w.put_u8(lobby.n);
            w.put_u8(0); // reserved
            for i in 0..(lobby.n as usize) {
                w.put_u32(lobby.ids[i]);
            }
        }
        RC_MSG_LOBBIES => {
            let lobbies = &m.u.lobbies;
            if (lobbies.n as usize) > RC_LOBBIES_MAX { return -1; }
            w.put_u16(lobbies.req);
            w.put_u8(lobbies.n);
            let len = (lobbies.n as usize) * RC_LOBBIES_ENTRY;
            if len > 0 && !lobbies.data.is_null() {
                let data_slice = std::slice::from_raw_parts(lobbies.data, len);
                w.put_bytes(data_slice);
            }
        }
        RC_MSG_RACEREP => {
            let rep = &m.u.racerep;
            if !racerep_ok(rep) { return -1; }
            w.put_u32(rep.lobby);
            w.put_u32(rep.race);
            w.put_u8(rep.mode);
            w.put_u8(rep.laps);
            w.put_u16(rep.track);
            w.put_u8(rep.place);
            w.put_u8(rep.players);
            w.put_u8(rep.flags);
            w.put_u8(rep.r#type);
            w.put_u8(rep.cls as u8);
            w.put_u8(0); w.put_u8(0); w.put_u8(0);
            w.put_f32(rep.rating);
            w.put_f32(rep.field);
            w.put_f32(rep.factor);
            w.put_f32(rep.kw);
            w.put_f32(rep.kg);
            w.put_f32(rep.kmh);
            w.put_f32(rep.time);
            let mut nm = [0u8; RC_RACEREP_NAME];
            for i in 0..RC_RACEREP_NAME {
                if rep.name[i] == 0 { break; }
                nm[i] = rep.name[i] as u8;
            }
            w.put_bytes(&nm);
        }
        RC_MSG_SAY => {
            w.put_u8(m.u.say.phrase);
            w.put_u8(m.u.say.seq);
            w.put_u8(0);
            w.put_u8(0);
        }
        _ => return -1,
    }

    if w.has_error() || w.len() + RC_CRC > max_len {
        return -1;
    }
    let total_len = (w.len() + RC_CRC) as u16;
    w.patch_u16(6, total_len);

    let crc = rc_crc32(out, w.len() as u32);
    w.put_u32(crc);

    if w.has_error() { -1 } else { w.len() as c_int }
}

/// Decodes and validates one datagram into `m`. Returns `RC_OK` or `RC_E_*`.
#[no_mangle]
pub unsafe extern "C" fn rc_decode(buf: *const u8, len: c_int, m: *mut rc_msg) -> c_int {
    if buf.is_null() || m.is_null() {
        return RC_E_SHORT;
    }
    let m = &mut *m;
    *m = rc_msg::default();

    let ulen = len as usize;
    if ulen < (RC_HDR + RC_CRC) {
        return RC_E_SHORT;
    }
    if ulen > RC_MAX_DGRAM {
        return RC_E_LENGTH;
    }

    let input = std::slice::from_raw_parts(buf, ulen);
    if input[0] != RC_MAGIC0 || input[1] != RC_MAGIC1 || input[2] != RC_MAGIC2 || input[3] != RC_MAGIC3 {
        return RC_E_MAGIC;
    }

    let payload_len = ulen - RC_CRC;
    let expected_crc = u32::from_le_bytes([input[ulen - 4], input[ulen - 3], input[ulen - 2], input[ulen - 1]]);
    if expected_crc != rc_crc32(buf, payload_len as u32) {
        return RC_E_CRC;
    }

    let mut r = PacketReader::new(&input[..payload_len]);
    r.skip(4); // Skip magic bytes

    m.h.ver = r.get_u8();
    if m.h.ver != RC_PROTO_VER {
        return RC_E_VERSION;
    }
    m.h.r#type = r.get_u8();
    m.h.len = r.get_u16();
    m.h.sender = r.get_u32();
    m.h.seq = r.get_u16();
    m.h.flags = r.get_u16();

    if (m.h.len as usize) != ulen {
        return RC_E_LENGTH;
    }
    if m.h.sender == 0 {
        return RC_E_FIELD;
    }

    match m.h.r#type {
        RC_MSG_HELLO => {
            let hello = &mut m.u.hello;
            hello.core_ver = r.get_u16();
            hello.platform = r.get_u8();
            hello.status = r.get_u8();
            hello.listen_port = r.get_u16();
            hello.cfg_rev = r.get_u16();
            hello.room = r.get_u32();
            let nl = r.get_u8() as usize;
            if nl > RC_NAME_MAX {
                return RC_E_FIELD;
            }
            let name_bytes = r.get_slice(nl);
            if r.has_error() { return RC_E_SHORT; }
            if !is_printable(name_bytes) { return RC_E_FIELD; }

            for i in 0..nl {
                hello.name[i] = name_bytes[i] as c_char;
            }
            hello.name[nl] = 0;

            if rc_name_blocked(hello.name.as_ptr()) != 0 {
                let default_name = b"Player\0";
                for (i, &b) in default_name.iter().enumerate() {
                    hello.name[i] = b as c_char;
                }
            }

            hello.n_seen = r.get_u8();
            if (hello.n_seen as usize) > RC_MAX_PEERS { return RC_E_FIELD; }
            for i in 0..(hello.n_seen as usize) {
                hello.seen[i] = r.get_u32();
            }
            if hello.status >= RC_ST__N { return RC_E_FIELD; }

            if (m.h.flags & RC_HF_CREW) != 0 && !r.has_error() && r.remaining() > 0 {
                hello.n_crews = r.get_u8();
                if (hello.n_crews as usize) > RC_CREW_MAX { return RC_E_FIELD; }
                for i in 0..(hello.n_crews as usize) {
                    hello.crews[i] = r.get_u32();
                }
                if r.has_error() { return RC_E_SHORT; }
                for i in 0..(hello.n_crews as usize) {
                    if hello.crews[i] == 0 { return RC_E_FIELD; }
                }
                let rem = r.remaining();
                r.skip(rem);
            }
        }
        RC_MSG_PEERS => {
            let peers = &mut m.u.peers;
            peers.n = r.get_u8();
            if (peers.n as usize) > RC_MAX_PEERS { return RC_E_FIELD; }
            for i in 0..(peers.n as usize) {
                let a = r.get_u8() as u32;
                let b = r.get_u8() as u32;
                let c = r.get_u8() as u32;
                let d = r.get_u8() as u32;
                peers.e[i].ip = (a << 24) | (b << 16) | (c << 8) | d;
                peers.e[i].port = r.get_u16();
                peers.e[i].id = r.get_u32();
                if !r.has_error() && (peers.e[i].port == 0 || peers.e[i].id == 0) {
                    return RC_E_FIELD;
                }
            }
            if (m.h.flags & RC_HF_CREW) != 0 && !r.has_error() && r.remaining() > 0 {
                peers.n_mine = r.get_u8();
                if !r.has_error() && (peers.n_mine as usize) > RC_CREW_MAX { return RC_E_FIELD; }
                for i in 0..(peers.n_mine as usize) {
                    peers.mine[i] = r.get_u32();
                }
                for i in 0..(peers.n as usize) {
                    peers.crew[i] = r.get_u8();
                }
                if r.has_error() { return RC_E_SHORT; }
                for i in 0..(peers.n_mine as usize) {
                    if peers.mine[i] == 0 { return RC_E_FIELD; }
                }
                let rem = r.remaining();
                r.skip(rem);
            }
        }
        RC_MSG_SNAP => {
            let snap = &mut m.u.snap;
            snap.status = r.get_u8();
            snap.epoch = r.get_u8();
            snap.cfg_rev = r.get_u16();
            snap.n = r.get_u8();
            snap.clock = r.get_u8();
            if r.has_error() { return RC_E_SHORT; }
            if snap.n < 1 || (snap.n as usize) > RC_SNAP_STATES || snap.status >= RC_ST__N {
                return RC_E_FIELD;
            }
            for i in 0..(snap.n as usize) {
                let err = get_state(&mut r, &mut snap.s[i]);
                if err != RC_OK { return err; }
                if (m.h.flags & RC_HF_STATEX) != 0 {
                    let err = get_statex(&mut r, &mut snap.s[i]);
                    if err != RC_OK { return err; }
                }
            }
        }
        RC_MSG_CFG => {
            let cfg = &mut m.u.cfg;
            cfg.cfg_rev = r.get_u16();
            cfg.frag = r.get_u8();
            cfg.nfrag = r.get_u8();
            cfg.total = r.get_u16();
            cfg.dlen = r.get_u16();
            if r.has_error() { return RC_E_SHORT; }
            if cfg.nfrag < 1 || (cfg.nfrag as usize) > RC_CFG_MAXFRAG || cfg.frag >= cfg.nfrag
                || (cfg.total as usize) > RC_CFG_MAX || (cfg.dlen as usize) > RC_CFG_FRAG {
                return RC_E_FIELD;
            }
            let total = cfg.total as usize;
            let nf = (total + RC_CFG_FRAG - 1) / RC_CFG_FRAG;
            let off = (cfg.frag as usize) * RC_CFG_FRAG;
            if total == 0 || nf != (cfg.nfrag as usize) { return RC_E_FIELD; }
            let want = if (cfg.frag as usize) + 1 < nf { RC_CFG_FRAG } else { total - off };
            if (cfg.dlen as usize) != want { return RC_E_FIELD; }
            if r.remaining() < (cfg.dlen as usize) { return RC_E_SHORT; }
            let data_ptr = input.as_ptr().add(r.position());
            cfg.data = data_ptr;
            r.skip(cfg.dlen as usize);
        }
        RC_MSG_CFG_REQ => {
            m.u.cfgreq.cfg_rev = r.get_u16();
            m.u.cfgreq.have_mask = r.get_u8();
        }
        RC_MSG_PING => {
            m.u.ping.t0 = r.get_u32();
        }
        RC_MSG_PONG => {
            m.u.ping.t0 = r.get_u32();
            m.u.ping.t1 = r.get_u32();
            m.u.ping.t2 = r.get_u32();
        }
        RC_MSG_BYE => {
            m.u.bye.reason = r.get_u8();
        }
        RC_MSG_DUEL => {
            let duel = &mut m.u.duel;
            duel.op = r.get_u8();
            duel.arg = r.get_u8();
            duel.to = r.get_u32();
            duel.duel = r.get_u32();
            if r.has_error() { return RC_E_SHORT; }
            if duel.op < 1 || duel.op >= RC_DUEL__N || duel.to == 0 || duel.duel == 0 {
                return RC_E_FIELD;
            }
        }
        RC_MSG_MAPREQ => {
            m.u.mapreq.req = r.get_u16();
        }
        RC_MSG_MAP => {
            let map = &mut m.u.map;
            map.req = r.get_u16();
            map.part = r.get_u8();
            map.nparts = r.get_u8();
            map.total = r.get_u16();
            map.n = r.get_u8();
            if r.has_error() { return RC_E_SHORT; }
            if map.nparts < 1 || (map.nparts as usize) > RC_MAP_PARTS || map.part >= map.nparts || (map.n as usize) > RC_MAP_PER {
                return RC_E_FIELD;
            }
            let len = (map.n as usize) * RC_MAP_ENTRY;
            if r.remaining() < len { return RC_E_SHORT; }
            map.data = input.as_ptr().add(r.position());
            r.skip(len);
        }
        RC_MSG_LOBBY => {
            let lobby = &mut m.u.lobby;
            lobby.op = r.get_u8();
            lobby.arg = r.get_u8();
            lobby.state = r.get_u8();
            lobby.max = r.get_u8();
            lobby.to = r.get_u32();
            lobby.lobby = r.get_u32();
            lobby.race = r.get_u32();
            lobby.x = r.get_u16() as i16;
            lobby.y = r.get_u16() as i16;
            lobby.t_ms = r.get_u16();
            lobby.n = r.get_u8();
            let _ = r.get_u8(); // reserved
            if r.has_error() { return RC_E_SHORT; }
            if (lobby.n as usize) > RC_LOBBY_MAX { return RC_E_FIELD; }
            for i in 0..(lobby.n as usize) {
                lobby.ids[i] = r.get_u32();
            }
            if r.has_error() { return RC_E_SHORT; }
            if !lobby_ok(lobby) { return RC_E_FIELD; }
        }
        RC_MSG_LOBBIES => {
            let lobbies = &mut m.u.lobbies;
            lobbies.req = r.get_u16();
            lobbies.n = r.get_u8();
            if r.has_error() { return RC_E_SHORT; }
            if (lobbies.n as usize) > RC_LOBBIES_MAX { return RC_E_FIELD; }
            let len = (lobbies.n as usize) * RC_LOBBIES_ENTRY;
            if r.remaining() < len { return RC_E_SHORT; }
            lobbies.data = input.as_ptr().add(r.position());
            r.skip(len);
        }
        RC_MSG_RACEREP => {
            let q = &mut m.u.racerep;
            q.lobby = r.get_u32();
            q.race = r.get_u32();
            q.mode = r.get_u8();
            q.laps = r.get_u8();
            q.track = r.get_u16();
            q.place = r.get_u8();
            q.players = r.get_u8();
            q.flags = r.get_u8();
            q.r#type = r.get_u8();
            q.cls = r.get_u8() as i8;
            let _ = r.get_u8();
            let _ = r.get_u8();
            let _ = r.get_u8();
            q.rating = r.get_f32();
            q.field = r.get_f32();
            q.factor = r.get_f32();
            q.kw = r.get_f32();
            q.kg = r.get_f32();
            q.kmh = r.get_f32();
            q.time = r.get_f32();
            let name_bytes = r.get_slice(RC_RACEREP_NAME);
            if r.has_error() { return RC_E_SHORT; }
            for i in 0..RC_RACEREP_NAME {
                q.name[i] = name_bytes[i] as c_char;
            }
            q.name[RC_RACEREP_NAME] = 0;
            if !racerep_ok(q) { return RC_E_FIELD; }
        }
        RC_MSG_SAY => {
            m.u.say.phrase = r.get_u8();
            m.u.say.seq = r.get_u8();
            let _ = r.get_u8();
            let _ = r.get_u8();
        }
        _ => return RC_E_TYPE,
    }

    if r.has_error() { return RC_E_SHORT; }
    if r.remaining() != 0 { return RC_E_TRAIL; }
    RC_OK
}
