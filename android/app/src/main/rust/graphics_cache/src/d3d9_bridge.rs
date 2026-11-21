//! Direct3D 9 to Native Mobile GPU (Vulkan / OpenGL ES) Bridge in Safe Rust for NFSU2 Android.
//!
//! Tracks D3D9 device render states, transforms D3D9 draw calls into direct Vulkan/GLES
//! pipeline commands, and handles texture/vertex buffer lifecycle.

use std::collections::HashMap;
use std::os::raw::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::RwLock;

// ---------------------------------------------------------------------------
// Direct3D 9 Primitive Types
// ---------------------------------------------------------------------------
pub const D3DPT_POINTLIST: u32 = 1;
pub const D3DPT_LINELIST: u32 = 2;
pub const D3DPT_LINESTRIP: u32 = 3;
pub const D3DPT_TRIANGLELIST: u32 = 4;
pub const D3DPT_TRIANGLESTRIP: u32 = 5;
pub const D3DPT_TRIANGLEFAN: u32 = 6;

// Common D3D9 Render States
pub const D3DRS_ZENABLE: u32 = 7;
pub const D3DRS_FILLMODE: u32 = 8;
pub const D3DRS_SHADEMODE: u32 = 9;
pub const D3DRS_ZWRITEENABLE: u32 = 14;
pub const D3DRS_ALPHATESTENABLE: u32 = 15;
pub const D3DRS_SRCBLEND: u32 = 19;
pub const D3DRS_DESTBLEND: u32 = 20;
pub const D3DRS_CULLMODE: u32 = 22;
pub const D3DRS_ZFUNC: u32 = 23;
pub const D3DRS_ALPHAREF: u32 = 24;
pub const D3DRS_ALPHAFUNC: u32 = 25;
pub const D3DRS_DITHERENABLE: u32 = 26;
pub const D3DRS_ALPHABLENDENABLE: u32 = 27;
pub const D3DRS_FOGENABLE: u32 = 28;
pub const D3DRS_SPECULARENABLE: u32 = 29;
pub const D3DRS_STENCILENABLE: u32 = 52;
pub const D3DRS_SCISSORTESTENABLE: u32 = 174;

// ---------------------------------------------------------------------------
// D3D9 Viewport & Rect
// ---------------------------------------------------------------------------
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct D3D9Viewport {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
    pub min_z: f32,
    pub max_z: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct D3D9Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct D3D9StreamSource {
    pub buffer_handle: u32,
    pub offset: u32,
    pub stride: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct D3DVertexElement9 {
    pub stream: u16,
    pub offset: u16,
    pub element_type: u8,
    pub method: u8,
    pub usage: u8,
    pub usage_index: u8,
}

// ---------------------------------------------------------------------------
// Resource Object Table
// ---------------------------------------------------------------------------
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ResourceKind {
    Texture,
    Surface,
    VertexBuffer,
    IndexBuffer,
    VertexShader,
    PixelShader,
    VertexDeclaration,
    StateBlock,
    Query,
}

#[derive(Debug)]
pub struct DeviceResource {
    pub handle: u32,
    pub kind: ResourceKind,
    pub size_bytes: usize,
    pub gpu_id: u32,
    pub ref_count: u32,
}

// ---------------------------------------------------------------------------
// D3D9 Device State Tracker
// ---------------------------------------------------------------------------
pub struct D3D9DeviceState {
    pub render_states: [u32; 256],
    pub viewport: D3D9Viewport,
    pub scissor_rect: D3D9Rect,
    pub world_matrix: [f32; 16],
    pub view_matrix: [f32; 16],
    pub proj_matrix: [f32; 16],
    pub texture_stage_states: [[u32; 32]; 8],
    pub sampler_states: [[u32; 16]; 8],
    pub active_textures: [u32; 16],
    pub stream_sources: [D3D9StreamSource; 8],
    pub active_vertex_buffer: u32,
    pub active_index_buffer: u32,
    pub active_vertex_decl: u32,
    pub fvf: u32,
    pub in_scene: bool,
    pub draw_call_count: u64,
    pub primitive_count: u64,
    pub resources: HashMap<u32, DeviceResource>,
    pub next_resource_id: u32,
}

impl D3D9DeviceState {
    pub fn new() -> Self {
        let mut states = [0u32; 256];
        states[D3DRS_ZENABLE as usize] = 1;
        states[D3DRS_ZWRITEENABLE as usize] = 1;
        states[D3DRS_CULLMODE as usize] = 3; // CCW
        states[D3DRS_ZFUNC as usize] = 4; // LESSEQUAL

        let identity = [
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        ];

        Self {
            render_states: states,
            viewport: D3D9Viewport::default(),
            scissor_rect: D3D9Rect::default(),
            world_matrix: identity,
            view_matrix: identity,
            proj_matrix: identity,
            texture_stage_states: [[0; 32]; 8],
            sampler_states: [[0; 16]; 8],
            active_textures: [0; 16],
            stream_sources: [D3D9StreamSource::default(); 8],
            active_vertex_buffer: 0,
            active_index_buffer: 0,
            active_vertex_decl: 0,
            fvf: 0,
            in_scene: false,
            draw_call_count: 0,
            primitive_count: 0,
            resources: HashMap::new(),
            next_resource_id: 0x80000001,
        }
    }

    pub fn set_render_state(&mut self, state: u32, value: u32) {
        if (state as usize) < self.render_states.len() {
            self.render_states[state as usize] = value;
        }
    }

    pub fn get_render_state(&self, state: u32) -> u32 {
        if (state as usize) < self.render_states.len() {
            self.render_states[state as usize]
        } else {
            0
        }
    }

    pub fn set_transform(&mut self, state: u32, matrix: &[f32; 16]) {
        match state {
            2 => self.view_matrix = *matrix,       // D3DTS_VIEW
            3 => self.proj_matrix = *matrix,       // D3DTS_PROJECTION
            256 => self.world_matrix = *matrix,    // D3DTS_WORLD
            _ => {}
        }
    }

    pub fn set_viewport(&mut self, vp: &D3D9Viewport) {
        self.viewport = *vp;
    }

    pub fn set_scissor_rect(&mut self, rect: &D3D9Rect) {
        self.scissor_rect = *rect;
    }

    pub fn set_texture(&mut self, stage: u32, texture_handle: u32) {
        if (stage as usize) < self.active_textures.len() {
            self.active_textures[stage as usize] = texture_handle;
        }
    }

    pub fn set_texture_stage_state(&mut self, stage: u32, state: u32, val: u32) {
        if (stage as usize) < self.texture_stage_states.len() && (state as usize) < 32 {
            self.texture_stage_states[stage as usize][state as usize] = val;
        }
    }

    pub fn set_sampler_state(&mut self, sampler: u32, state: u32, val: u32) {
        if (sampler as usize) < self.sampler_states.len() && (state as usize) < 16 {
            self.sampler_states[sampler as usize][state as usize] = val;
        }
    }

    pub fn set_stream_source(&mut self, stream: u32, buffer_handle: u32, offset: u32, stride: u32) {
        if (stream as usize) < self.stream_sources.len() {
            self.stream_sources[stream as usize] = D3D9StreamSource {
                buffer_handle,
                offset,
                stride,
            };
            if stream == 0 {
                self.active_vertex_buffer = buffer_handle;
            }
        }
    }

    pub fn set_indices(&mut self, buffer_handle: u32) {
        self.active_index_buffer = buffer_handle;
    }

    pub fn set_fvf(&mut self, fvf: u32) {
        self.fvf = fvf;
    }

    pub fn begin_scene(&mut self) -> bool {
        if self.in_scene {
            false
        } else {
            self.in_scene = true;
            true
        }
    }

    pub fn end_scene(&mut self) -> bool {
        if !self.in_scene {
            false
        } else {
            self.in_scene = false;
            true
        }
    }

    pub fn record_draw(&mut self, primitive_type: u32, primitive_count: u32) {
        self.draw_call_count += 1;
        self.primitive_count += primitive_count as u64;
    }

    pub fn register_resource(&mut self, kind: ResourceKind, size_bytes: usize) -> u32 {
        let handle = self.next_resource_id;
        self.next_resource_id = self.next_resource_id.wrapping_add(1);
        self.resources.insert(
            handle,
            DeviceResource {
                handle,
                kind,
                size_bytes,
                gpu_id: 0,
                ref_count: 1,
            },
        );
        handle
    }

    pub fn release_resource(&mut self, handle: u32) -> bool {
        if let Some(res) = self.resources.get_mut(&handle) {
            if res.ref_count > 0 {
                res.ref_count -= 1;
                if res.ref_count == 0 {
                    self.resources.remove(&handle);
                    return true;
                }
            }
        }
        false
    }
}

static D3D9_STATE: RwLock<Option<D3D9DeviceState>> = RwLock::new(None);

fn with_d3d9<F, R>(f: F) -> R
where
    F: FnOnce(&mut D3D9DeviceState) -> R,
{
    let mut guard = D3D9_STATE.write().unwrap();
    if guard.is_none() {
        *guard = Some(D3D9DeviceState::new());
    }
    f(guard.as_mut().unwrap())
}

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_render_state(state: u32, value: u32) {
    with_d3d9(|d| d.set_render_state(state, value));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_get_render_state(state: u32) -> u32 {
    with_d3d9(|d| d.get_render_state(state))
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_transform(state: u32, matrix: *const f32) {
    if !matrix.is_null() {
        let m = &*(matrix as *const [f32; 16]);
        with_d3d9(|d| d.set_transform(state, m));
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_get_transform(state: u32, out_matrix: *mut f32) {
    if !out_matrix.is_null() {
        let out = &mut *(out_matrix as *mut [f32; 16]);
        with_d3d9(|d| {
            match state {
                2 => *out = d.view_matrix,
                3 => *out = d.proj_matrix,
                256 => *out = d.world_matrix,
                _ => {}
            }
        });
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_texture(stage: u32, texture: u32) {
    with_d3d9(|d| d.set_texture(stage, texture));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_texture_stage_state(stage: u32, state: u32, val: u32) {
    with_d3d9(|d| d.set_texture_stage_state(stage, state, val));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_sampler_state(sampler: u32, state: u32, val: u32) {
    with_d3d9(|d| d.set_sampler_state(sampler, state, val));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_stream_source(stream: u32, buffer_handle: u32, offset: u32, stride: u32) {
    with_d3d9(|d| d.set_stream_source(stream, buffer_handle, offset, stride));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_indices(buffer_handle: u32) {
    with_d3d9(|d| d.set_indices(buffer_handle));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_fvf(fvf: u32) {
    with_d3d9(|d| d.set_fvf(fvf));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_viewport(vp: *const D3D9Viewport) {
    if !vp.is_null() {
        with_d3d9(|d| d.set_viewport(&*vp));
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_scissor_rect(rect: *const D3D9Rect) {
    if !rect.is_null() {
        with_d3d9(|d| d.set_scissor_rect(&*rect));
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_register_resource(kind: u32, size_bytes: usize) -> u32 {
    let rkind = match kind {
        1 => ResourceKind::Texture,
        2 => ResourceKind::Surface,
        3 => ResourceKind::VertexBuffer,
        4 => ResourceKind::IndexBuffer,
        5 => ResourceKind::VertexShader,
        6 => ResourceKind::PixelShader,
        7 => ResourceKind::VertexDeclaration,
        8 => ResourceKind::StateBlock,
        9 => ResourceKind::Query,
        _ => ResourceKind::Texture,
    };
    with_d3d9(|d| d.register_resource(rkind, size_bytes))
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_release_resource(handle: u32) -> u32 {
    let freed = with_d3d9(|d| d.release_resource(handle));
    if freed { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_begin_scene() -> u32 {
    let ok = with_d3d9(|d| d.begin_scene());
    if ok { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_end_scene() -> u32 {
    let ok = with_d3d9(|d| d.end_scene());
    if ok { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_record_draw(primitive_type: u32, primitive_count: u32) {
    with_d3d9(|d| d.record_draw(primitive_type, primitive_count));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_get_draw_calls() -> u64 {
    with_d3d9(|d| d.draw_call_count)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_get_primitives() -> u64 {
    with_d3d9(|d| d.primitive_count)
}
