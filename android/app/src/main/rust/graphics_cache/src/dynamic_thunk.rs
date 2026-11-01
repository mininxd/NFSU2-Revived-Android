//! Decoded DynamicThunk Bytecode Cache & Fast Execution Engine.
//!
//! Replaces byte-by-byte opcode interpretation of dynamic x86 audio mixing code
//! with a pre-decoded instruction stream. Resolves lifted function pointers once
//! and eliminates MachineMutex lock starvation on the render thread.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

pub type RecompLookupFn = unsafe extern "C" fn(u32) -> usize;
pub type CallLiftedFn = unsafe extern "C" fn(usize, u32) -> u32;

#[repr(C)]
pub struct ThunkContext {
    pub start_addr: u32,
    pub eax: u32,
    pub ecx: u32,
    pub esi: u32,
    pub esp: u32,
    pub cur_func: u32,
    pub mem_ptr: *mut u8,
    pub mem_size: usize,
    pub lookup_fn: Option<RecompLookupFn>,
    pub call_fn: Option<CallLiftedFn>,
}

#[derive(Clone, Copy, Debug)]
pub enum DecodedOp {
    CallLifted { target: u32, fn_ptr: usize },
    AddEsi(i32),
    AddEsp(u32),
    StoreEaxToEsi(u32),
    ReloadEsiFromEsp,
    LoadEaxFromEsi(u32),
    LoadEcxFromEsi(u32),
    MovEaxEcx,
    CmpEaxEcx,
    MulEaxFromEsi(u32),
    BranchCond { op: u8, target_ip: usize },
    ClearByteAtEsi,
    AddEaxFromEsi(u32),
    SubEaxFromEsi(u32),
    Return,
}

pub struct DecodedThunk {
    pub start_addr: u32,
    pub ops: Vec<DecodedOp>,
}

impl DecodedThunk {
    pub unsafe fn decode(
        start: u32,
        mem: *const u8,
        mem_size: usize,
        lookup: RecompLookupFn,
    ) -> Result<Self, &'static str> {
        let read8 = |addr: u32| -> Result<u8, &'static str> {
            if (addr as usize) < mem_size {
                Ok(*mem.add(addr as usize))
            } else {
                Err("Address out of guest memory bounds")
            }
        };

        let read32 = |addr: u32| -> Result<u32, &'static str> {
            if (addr as usize + 3) < mem_size {
                let p = mem.add(addr as usize) as *const u32;
                Ok(p.read_unaligned())
            } else {
                Err("Address out of guest memory bounds")
            }
        };

        if read32(start)? != 0x24748b56 || read8(start + 4)? != 8 {
            return Err("Invalid generated callback header");
        }

        let mut pc = start;
        let mut saved = false;
        let mut arguments = 0u32;
        let mut total_arguments = 0u32;
        let mut pending = false;
        let mut stack_cleared = false;
        let mut popped = false;
        let mut compared = false;

        let mut raw_ops = Vec::new();
        let mut pc_to_op_idx = HashMap::new();
        let mut branch_fixups = Vec::new();

        for _ in 0..4096 {
            if pc - start >= 65536 {
                break;
            }
            let inst_pc = pc;
            let op_idx = raw_ops.len();
            pc_to_op_idx.insert(inst_pc, op_idx);

            let op = read8(pc)?;
            pc += 1;

            if op == 0x56 {
                if !saved {
                    saved = true;
                } else {
                    arguments += 1;
                    total_arguments += 1;
                    pending = true;
                    stack_cleared = false;
                }
                continue;
            }

            if op == 0xe8 && pending {
                let delta = read32(pc)? as i32;
                pc += 4;
                let target = pc.wrapping_add(delta as u32);
                let fn_ptr = lookup(target);
                if fn_ptr == 0 {
                    return Err("Generated callback calls unlifted function");
                }
                raw_ops.push(DecodedOp::CallLifted { target, fn_ptr });
                pending = false;
                continue;
            }

            if op == 0x83 {
                let form = read8(pc)?;
                let delta = read8(pc + 1)? as i8;
                pc += 2;
                if form == 0xc6 {
                    raw_ops.push(DecodedOp::AddEsi(delta as i32));
                    compared = false;
                    continue;
                }
                if form == 0xc4 && (delta as i32 as u32) == arguments * 4 && !pending {
                    raw_ops.push(DecodedOp::AddEsp(delta as i32 as u32));
                    stack_cleared = true;
                    arguments = 0;
                    compared = false;
                    continue;
                }
            }

            if op == 0x81 {
                let form = read8(pc)?;
                let delta = read32(pc + 1)?;
                pc += 5;
                if form == 0xc6 {
                    raw_ops.push(DecodedOp::AddEsi(delta as i32));
                    compared = false;
                    continue;
                }
                if form == 0xc4 && delta == arguments * 4 && !pending {
                    raw_ops.push(DecodedOp::AddEsp(delta));
                    stack_cleared = true;
                    arguments = 0;
                    compared = false;
                    continue;
                }
            }

            if op == 0x89 {
                let form = read8(pc)?;
                pc += 1;
                let offset = if form == 0x86 {
                    let val = read32(pc)?;
                    pc += 4;
                    val
                } else if form == 0x46 {
                    let val = read8(pc)? as u32;
                    pc += 1;
                    val
                } else {
                    return Err("Unsupported store form in generated callback");
                };
                raw_ops.push(DecodedOp::StoreEaxToEsi(offset));
                continue;
            }

            if op == 0x8b {
                let form = read8(pc)?;
                pc += 1;
                if form == 0x74 || form == 0xb4 {
                    let sib = read8(pc)?;
                    pc += 1;
                    let offset = if form == 0x74 {
                        let val = read8(pc)? as i8 as i32 as u32;
                        pc += 1;
                        val
                    } else {
                        let val = read32(pc)?;
                        pc += 4;
                        val
                    };
                    if sib == 0x24 && saved && offset == 4 * (1 + arguments) + 4 {
                        raw_ops.push(DecodedOp::ReloadEsiFromEsp);
                        continue;
                    }
                }
                if form == 0x46 {
                    let off = read8(pc)? as u32;
                    pc += 1;
                    raw_ops.push(DecodedOp::LoadEaxFromEsi(off));
                    continue;
                }
                if form == 0x06 {
                    raw_ops.push(DecodedOp::LoadEaxFromEsi(0));
                    continue;
                }
                if form == 0x4e {
                    let off = read8(pc)? as i8 as i32 as u32;
                    pc += 1;
                    raw_ops.push(DecodedOp::LoadEcxFromEsi(off));
                    continue;
                }
                if form == 0x8e {
                    let off = read32(pc)?;
                    pc += 4;
                    raw_ops.push(DecodedOp::LoadEcxFromEsi(off));
                    continue;
                }
                if form == 0x0e {
                    raw_ops.push(DecodedOp::LoadEcxFromEsi(0));
                    continue;
                }
                if form == 0xc1 {
                    raw_ops.push(DecodedOp::MovEaxEcx);
                    continue;
                }
            }

            if op == 0x3b && read8(pc)? == 0xc1 {
                pc += 1;
                raw_ops.push(DecodedOp::CmpEaxEcx);
                compared = true;
                continue;
            }

            if op == 0x0f && read8(pc)? == 0xaf {
                let form = read8(pc + 1)?;
                pc += 2;
                let off = if form == 0x46 {
                    let val = read8(pc)? as i8 as i32 as u32;
                    pc += 1;
                    val
                } else if form == 0x86 {
                    let val = read32(pc)?;
                    pc += 4;
                    val
                } else if form == 0x06 {
                    0
                } else {
                    return Err("Unsupported imul form in generated callback");
                };
                raw_ops.push(DecodedOp::MulEaxFromEsi(off));
                compared = false;
                continue;
            }

            if op >= 0x7c && op <= 0x7f && compared {
                let delta = read8(pc)? as i8;
                pc += 1;
                let target = pc.wrapping_add(delta as i32 as u32);
                let current_idx = raw_ops.len();
                raw_ops.push(DecodedOp::BranchCond {
                    op,
                    target_ip: 0, // Resolved below
                });
                branch_fixups.push((current_idx, target));
                continue;
            }

            if op == 0xc6
                && read8(pc)? == 0x46
                && read8(pc + 1)? == 0
                && read8(pc + 2)? == 0
            {
                pc += 3;
                raw_ops.push(DecodedOp::ClearByteAtEsi);
                continue;
            }

            if op == 0x03 || op == 0x2b {
                let form = read8(pc)?;
                pc += 1;
                let off = if form == 0x46 {
                    let val = read8(pc)? as i8 as i32 as u32;
                    pc += 1;
                    val
                } else if form == 0x86 {
                    let val = read32(pc)?;
                    pc += 4;
                    val
                } else if form == 0x06 {
                    0
                } else {
                    return Err("Unsupported add/sub form in generated callback");
                };
                if op == 0x03 {
                    raw_ops.push(DecodedOp::AddEaxFromEsi(off));
                } else {
                    raw_ops.push(DecodedOp::SubEaxFromEsi(off));
                }
                compared = false;
                continue;
            }

            if op == 0x5e && stack_cleared && !pending {
                popped = true;
                continue;
            }

            if op == 0xc3 && popped && saved && total_arguments > 0 && arguments == 0 {
                raw_ops.push(DecodedOp::Return);
                break;
            }

            return Err("Unknown opcode in generated callback");
        }

        for (op_idx, target_pc) in branch_fixups {
            if let Some(&target_idx) = pc_to_op_idx.get(&target_pc) {
                if let DecodedOp::BranchCond { ref mut target_ip, .. } = raw_ops[op_idx] {
                    *target_ip = target_idx;
                }
            } else {
                return Err("Branch target outside decoded instruction boundaries");
            }
        }

        Ok(Self {
            start_addr: start,
            ops: raw_ops,
        })
    }

    pub unsafe fn execute(&self, ctx: &mut ThunkContext) -> Result<(), &'static str> {
        let mem = ctx.mem_ptr;
        let mem_size = ctx.mem_size;

        let read32 = |addr: u32| -> u32 {
            if (addr as usize + 3) < mem_size {
                let p = mem.add(addr as usize) as *const u32;
                p.read_unaligned()
            } else {
                0
            }
        };

        let write32 = |addr: u32, val: u32| {
            if (addr as usize + 3) < mem_size {
                let p = mem.add(addr as usize) as *mut u32;
                p.write_unaligned(val);
            }
        };

        let write8 = |addr: u32, val: u8| {
            if (addr as usize) < mem_size {
                *mem.add(addr as usize) = val;
            }
        };

        let original_esi = ctx.esi;
        let original_esp = ctx.esp;
        let mut esi = 0u32;
        let mut eax = ctx.eax;
        let mut ecx = ctx.ecx;
        let mut comparison = 0i32;

        let call_fn = match ctx.call_fn {
            Some(f) => f,
            None => return Err("Missing call function in context"),
        };

        let mut ip = 0;
        let ops = &self.ops;
        let ops_len = ops.len();

        while ip < ops_len {
            match ops[ip] {
                DecodedOp::CallLifted { fn_ptr, .. } => {
                    eax = call_fn(fn_ptr, esi);
                }
                DecodedOp::AddEsi(delta) => {
                    esi = esi.wrapping_add(delta as u32);
                }
                DecodedOp::AddEsp(_) => {}
                DecodedOp::StoreEaxToEsi(offset) => {
                    write32(esi.wrapping_add(offset), eax);
                }
                DecodedOp::ReloadEsiFromEsp => {
                    esi = read32(original_esp + 4);
                }
                DecodedOp::LoadEaxFromEsi(offset) => {
                    eax = read32(esi.wrapping_add(offset));
                }
                DecodedOp::LoadEcxFromEsi(offset) => {
                    ecx = read32(esi.wrapping_add(offset));
                }
                DecodedOp::MovEaxEcx => {
                    eax = ecx;
                }
                DecodedOp::CmpEaxEcx => {
                    comparison = if (eax as i32) < (ecx as i32) {
                        -1
                    } else if (eax as i32) > (ecx as i32) {
                        1
                    } else {
                        0
                    };
                }
                DecodedOp::MulEaxFromEsi(offset) => {
                    let val = read32(esi.wrapping_add(offset));
                    eax = eax.wrapping_mul(val);
                }
                DecodedOp::BranchCond { op, target_ip } => {
                    let taken = match op {
                        0x7c => comparison < 0,
                        0x7d => comparison >= 0,
                        0x7e => comparison <= 0,
                        0x7f => comparison > 0,
                        _ => false,
                    };
                    if taken {
                        ip = target_ip;
                        continue;
                    }
                }
                DecodedOp::ClearByteAtEsi => {
                    write8(esi, 0);
                }
                DecodedOp::AddEaxFromEsi(offset) => {
                    let val = read32(esi.wrapping_add(offset));
                    eax = eax.wrapping_add(val);
                }
                DecodedOp::SubEaxFromEsi(offset) => {
                    let val = read32(esi.wrapping_add(offset));
                    eax = eax.wrapping_sub(val);
                }
                DecodedOp::Return => {
                    ctx.eax = eax;
                    ctx.ecx = ecx;
                    ctx.esi = original_esi;
                    ctx.esp = original_esp + 4;
                    return Ok(());
                }
            }
            ip += 1;
        }

        Err("Decoded callback reached end without return")
    }
}

static THUNK_CACHE: Mutex<Option<HashMap<u32, Arc<DecodedThunk>>>> = Mutex::new(None);

#[no_mangle]
pub unsafe extern "C" fn nfs_dynamic_thunk_run(ctx: *mut ThunkContext) -> i32 {
    if ctx.is_null() {
        return -1;
    }

    let c = &mut *ctx;
    let start = c.start_addr;

    let thunk = {
        let mut guard = THUNK_CACHE.lock().unwrap();
        let map = guard.get_or_insert_with(HashMap::new);
        if let Some(existing) = map.get(&start) {
            existing.clone()
        } else {
            let lookup = match c.lookup_fn {
                Some(f) => f,
                None => return -2,
            };
            match DecodedThunk::decode(start, c.mem_ptr, c.mem_size, lookup) {
                Ok(decoded) => {
                    let arc = Arc::new(decoded);
                    map.insert(start, arc.clone());
                    arc
                }
                Err(_) => return -3,
            }
        }
    };

    match thunk.execute(c) {
        Ok(()) => 0,
        Err(_) => -4,
    }
}
