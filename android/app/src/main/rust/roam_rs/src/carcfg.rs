//! Car appearance & tuning configuration record serialization and deserialization.

use std::os::raw::{c_char, c_int};
use crate::wire::{PacketReader, PacketWriter};

pub const RC_CFG_MAX: usize = 2048;
pub const RC_PART_SLOTS: usize = 170;
pub const RC_CFG_TYPE_NAME: usize = 16;
pub const RC_PERF_BYTES: usize = 52;
pub const RC_CFGREC_VER: u8 = 1;

pub const RC_EXT_PERF: u8 = 1;
pub const RC_EXT_HORN: u8 = 2;

pub const RC_OK: c_int = 0;
pub const RC_E_SHORT: c_int = -1;
pub const RC_E_VERSION: c_int = -3;
pub const RC_E_FIELD: c_int = -6;
pub const RC_E_TRAIL: c_int = -8;
pub const RC_E_LENGTH: c_int = -9;

#[repr(C)]
pub struct RcPart {
    pub slot: u8,
    pub key: u32,
    pub owner: u32,
}

#[repr(C)]
pub struct RcCarCfg {
    pub type_hash: u32,
    pub type_name: [c_char; RC_CFG_TYPE_NAME + 1],
    pub ride_b: [u8; 4],
    pub n_parts: u8,
    pub part: [RcPart; RC_PART_SLOTS],
    pub has_perf: u8,
    pub perf: [u8; RC_PERF_BYTES],
    pub has_horn: u8,
    pub horn: u8,
}

#[no_mangle]
pub extern "C" fn rc_carcfg_encode(c: *const RcCarCfg, out: *mut u8, cap: c_int) -> c_int {
    if c.is_null() || out.is_null() || cap <= 0 {
        return -1;
    }
    let cfg = unsafe { &*c };
    if cfg.n_parts as usize > RC_PART_SLOTS {
        return -1;
    }
    let write_cap = (cap as usize).min(RC_CFG_MAX);
    let slice = unsafe { std::slice::from_raw_parts_mut(out, write_cap) };
    let mut w = PacketWriter::new(slice);

    let mut tn = [0u8; RC_CFG_TYPE_NAME];
    for (i, slot) in tn.iter_mut().enumerate() {
        let ch = cfg.type_name[i] as u8;
        if ch == 0 {
            break;
        }
        *slot = ch;
    }

    w.put_u8(RC_CFGREC_VER);
    w.put_u32(cfg.type_hash);
    w.put_bytes(&tn);
    w.put_bytes(&cfg.ride_b);
    w.put_u8(cfg.n_parts);

    for i in 0..cfg.n_parts as usize {
        w.put_u8(cfg.part[i].slot);
        w.put_u32(cfg.part[i].key);
        w.put_u32(cfg.part[i].owner);
    }

    let ext_count = (if cfg.has_perf != 0 { 1u8 } else { 0 }) + (if cfg.has_horn != 0 { 1u8 } else { 0 });
    w.put_u8(ext_count);

    if cfg.has_perf != 0 {
        w.put_u8(RC_EXT_PERF);
        w.put_u8(RC_PERF_BYTES as u8);
        w.put_bytes(&cfg.perf);
    }
    if cfg.has_horn != 0 {
        w.put_u8(RC_EXT_HORN);
        w.put_u8(1);
        w.put_u8(cfg.horn);
    }

    if w.has_error() {
        -1
    } else {
        w.len() as c_int
    }
}

#[no_mangle]
pub extern "C" fn rc_carcfg_decode(p: *const u8, len: c_int, c: *mut RcCarCfg) -> c_int {
    if p.is_null() || c.is_null() {
        return RC_E_SHORT;
    }
    if len < 1 || len as usize > RC_CFG_MAX {
        return RC_E_LENGTH;
    }

    let cfg = unsafe {
        std::ptr::write_bytes(c, 0, 1);
        &mut *c
    };

    let slice = unsafe { std::slice::from_raw_parts(p, len as usize) };
    let mut r = PacketReader::new(slice);

    if r.get_u8() != RC_CFGREC_VER {
        return RC_E_VERSION;
    }
    cfg.type_hash = r.get_u32();

    let mut tn = [0u8; RC_CFG_TYPE_NAME];
    r.get_bytes(&mut tn);
    for (i, &b) in tn.iter().enumerate() {
        cfg.type_name[i] = b as c_char;
    }
    cfg.type_name[RC_CFG_TYPE_NAME] = 0;

    r.get_bytes(&mut cfg.ride_b);
    let n = r.get_u8() as usize;
    if r.has_error() {
        return RC_E_SHORT;
    }
    if n > RC_PART_SLOTS {
        return RC_E_FIELD;
    }

    let mut seen = [false; RC_PART_SLOTS];
    for i in 0..n {
        let slot = r.get_u8() as usize;
        let key = r.get_u32();
        let owner = r.get_u32();
        if r.has_error() {
            return RC_E_SHORT;
        }
        if slot >= RC_PART_SLOTS || seen[slot] {
            return RC_E_FIELD;
        }
        seen[slot] = true;
        cfg.part[i].slot = slot as u8;
        cfg.part[i].key = key;
        cfg.part[i].owner = owner;
    }
    cfg.n_parts = n as u8;

    let next_ext = r.get_u8() as usize;
    for _ in 0..next_ext {
        if r.has_error() {
            break;
        }
        let tag = r.get_u8();
        let l = r.get_u8() as usize;
        if tag == RC_EXT_PERF && l == RC_PERF_BYTES {
            r.get_bytes(&mut cfg.perf);
            cfg.has_perf = if !r.has_error() { 1 } else { 0 };
        } else if tag == RC_EXT_HORN && l == 1 {
            cfg.horn = r.get_u8();
            cfg.has_horn = if !r.has_error() { 1 } else { 0 };
        } else {
            let mut skip = [0u8; 256];
            let to_skip = l.min(skip.len());
            r.get_bytes(&mut skip[..to_skip]);
        }
    }

    if r.has_error() {
        RC_E_SHORT
    } else if r.remaining() != 0 {
        RC_E_TRAIL
    } else {
        RC_OK
    }
}
