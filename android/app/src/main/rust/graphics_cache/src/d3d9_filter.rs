//! D3D9 Redundant State Deduplication Shadow Filter.
//!
//! Statically linked D3DX effect passes in SPEED2.EXE issue 38k-75k D3D9 calls
//! per frame, with 15k-25k redundant state calls (SetRenderState, SetTexture,
//! SetSamplerState, SetTextureStageState, etc.).
//! This shadow filter intercepts redundant calls and returns D3D_OK immediately
//! without crossing into DXVK Native or Vulkan descriptor updates.

use std::sync::atomic::{AtomicU64, Ordering};

pub const D3D_OK: u32 = 0;

pub struct D3D9ShadowState {
    pub rs: [u32; 256],
    pub rs_valid: [bool; 256],
    pub ss: [[u32; 16]; 16],
    pub ss_valid: [[bool; 16]; 16],
    pub tss: [[u32; 33]; 8],
    pub tss_valid: [[bool; 33]; 8],
    pub tex: [u32; 16],
    pub tex_epoch: [u64; 16],
    pub tex_valid: [bool; 16],
    pub decl: u32,
    pub decl_epoch: u64,
    pub decl_valid: bool,
    pub fvf: u32,
    pub fvf_valid: bool,
    pub vs: u32,
    pub vs_epoch: u64,
    pub vs_valid: bool,
    pub ps: u32,
    pub ps_epoch: u64,
    pub ps_valid: bool,
    pub stream_buffer: [u32; 4],
    pub stream_offset: [u32; 4],
    pub stream_stride: [u32; 4],
    pub stream_epoch: [u64; 4],
    pub stream_valid: [bool; 4],
    pub ib: u32,
    pub ib_epoch: u64,
    pub ib_valid: bool,
    pub epoch: u64,
}

impl D3D9ShadowState {
    pub const fn new() -> Self {
        Self {
            rs: [0; 256],
            rs_valid: [false; 256],
            ss: [[0; 16]; 16],
            ss_valid: [[false; 16]; 16],
            tss: [[0; 33]; 8],
            tss_valid: [[false; 33]; 8],
            tex: [0; 16],
            tex_epoch: [0; 16],
            tex_valid: [false; 16],
            decl: 0,
            decl_epoch: 0,
            decl_valid: false,
            fvf: 0,
            fvf_valid: false,
            vs: 0,
            vs_epoch: 0,
            vs_valid: false,
            ps: 0,
            ps_epoch: 0,
            ps_valid: false,
            stream_buffer: [0; 4],
            stream_offset: [0; 4],
            stream_stride: [0; 4],
            stream_epoch: [0; 4],
            stream_valid: [false; 4],
            ib: 0,
            ib_epoch: 0,
            ib_valid: false,
            epoch: 0,
        }
    }

    pub fn forget(&mut self) {
        self.rs_valid = [false; 256];
        self.ss_valid = [[false; 16]; 16];
        self.tss_valid = [[false; 33]; 8];
        self.tex_valid = [false; 16];
        self.decl_valid = false;
        self.fvf_valid = false;
        self.vs_valid = false;
        self.ps_valid = false;
        self.stream_valid = [false; 4];
        self.ib_valid = false;
    }

    pub fn notify_retired(&mut self) {
        self.epoch = self.epoch.wrapping_add(1);
    }

    pub fn filter_method(&mut self, method: u32, args: &[u32], out_count: &mut u32) -> bool {
        match method {
            // SetRenderState(State, Value)
            57 => {
                *out_count = 3;
                let state = args[1] as usize;
                let val = args[2];
                if state < 256 {
                    if self.rs_valid[state] && self.rs[state] == val {
                        return true;
                    }
                    self.rs[state] = val;
                    self.rs_valid[state] = true;
                }
                false
            }
            // SetTexture(Stage, pTexture)
            65 => {
                *out_count = 3;
                let stage = args[1] as usize;
                let tex_handle = args[2];
                if stage < 16 {
                    if self.tex_valid[stage]
                        && self.tex[stage] == tex_handle
                        && self.tex_epoch[stage] == self.epoch
                    {
                        return true;
                    }
                    self.tex[stage] = tex_handle;
                    self.tex_epoch[stage] = self.epoch;
                    self.tex_valid[stage] = true;
                }
                false
            }
            // SetTextureStageState(Stage, Type, Value)
            67 => {
                *out_count = 4;
                let stage = args[1] as usize;
                let state_type = args[2] as usize;
                let val = args[3];
                if stage < 8 && state_type < 33 {
                    if self.tss_valid[stage][state_type] && self.tss[stage][state_type] == val {
                        return true;
                    }
                    self.tss[stage][state_type] = val;
                    self.tss_valid[stage][state_type] = true;
                }
                false
            }
            // SetSamplerState(Sampler, Type, Value)
            69 => {
                *out_count = 4;
                let sampler = args[1] as usize;
                let state_type = args[2] as usize;
                let val = args[3];
                if sampler < 16 && state_type < 16 {
                    if self.ss_valid[sampler][state_type] && self.ss[sampler][state_type] == val {
                        return true;
                    }
                    self.ss[sampler][state_type] = val;
                    self.ss_valid[sampler][state_type] = true;
                }
                false
            }
            // SetVertexDeclaration(pDecl)
            87 => {
                *out_count = 2;
                let decl_handle = args[1];
                if self.decl_valid && self.decl == decl_handle && self.decl_epoch == self.epoch {
                    return true;
                }
                self.decl = decl_handle;
                self.decl_epoch = self.epoch;
                self.decl_valid = true;
                self.fvf_valid = false;
                false
            }
            // SetFVF(FVF)
            89 => {
                *out_count = 2;
                let fvf_val = args[1];
                if self.fvf_valid && self.fvf == fvf_val {
                    return true;
                }
                self.fvf = fvf_val;
                self.fvf_valid = true;
                self.decl_valid = false;
                false
            }
            // SetVertexShader(pShader)
            92 => {
                *out_count = 2;
                let shader = args[1];
                if self.vs_valid && self.vs == shader && self.vs_epoch == self.epoch {
                    return true;
                }
                self.vs = shader;
                self.vs_epoch = self.epoch;
                self.vs_valid = true;
                false
            }
            // SetPixelShader(pShader)
            107 => {
                *out_count = 2;
                let shader = args[1];
                if self.ps_valid && self.ps == shader && self.ps_epoch == self.epoch {
                    return true;
                }
                self.ps = shader;
                self.ps_epoch = self.epoch;
                self.ps_valid = true;
                false
            }
            // SetStreamSource(Stream, pData, Offset, Stride)
            100 => {
                *out_count = 5;
                let stream = args[1] as usize;
                let buf = args[2];
                let offset = args[3];
                let stride = args[4];
                if stream < 4 {
                    if self.stream_valid[stream]
                        && self.stream_buffer[stream] == buf
                        && self.stream_offset[stream] == offset
                        && self.stream_stride[stream] == stride
                        && self.stream_epoch[stream] == self.epoch
                    {
                        return true;
                    }
                    self.stream_buffer[stream] = buf;
                    self.stream_offset[stream] = offset;
                    self.stream_stride[stream] = stride;
                    self.stream_epoch[stream] = self.epoch;
                    self.stream_valid[stream] = true;
                }
                false
            }
            // SetIndices(pIndexData)
            104 => {
                *out_count = 2;
                let ib_handle = args[1];
                if self.ib_valid && self.ib == ib_handle && self.ib_epoch == self.epoch {
                    return true;
                }
                self.ib = ib_handle;
                self.ib_epoch = self.epoch;
                self.ib_valid = true;
                false
            }
            _ => false,
        }
    }
}

static mut GLOBAL_SHADOW: D3D9ShadowState = D3D9ShadowState::new();
static TOTAL_CALLS: AtomicU64 = AtomicU64::new(0);
static FILTERED_CALLS: AtomicU64 = AtomicU64::new(0);

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_filter_state(
    method: u32,
    args: *const u32,
    count: *mut u32,
) -> i32 {
    if args.is_null() || count.is_null() {
        return 0;
    }

    TOTAL_CALLS.fetch_add(1, Ordering::Relaxed);
    let arg_slice = std::slice::from_raw_parts(args, 6);
    let mut out_count = 0u32;
    let is_redundant = GLOBAL_SHADOW.filter_method(method, arg_slice, &mut out_count);

    if is_redundant {
        *count = out_count;
        FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_filter_forget_shadow() {
    GLOBAL_SHADOW.forget();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_filter_notify_retired() {
    GLOBAL_SHADOW.notify_retired();
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_filter_stats(out_total: *mut u64, out_filtered: *mut u64) {
    if !out_total.is_null() {
        *out_total = TOTAL_CALLS.load(Ordering::Relaxed);
    }
    if !out_filtered.is_null() {
        *out_filtered = FILTERED_CALLS.load(Ordering::Relaxed);
    }
}
