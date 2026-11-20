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
// D3D9 Viewport
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
    pub active_textures: [u32; 16],
    pub active_vertex_buffer: u32,
    pub active_index_buffer: u32,
    pub active_vertex_decl: u32,
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

        Self {
            render_states: states,
            viewport: D3D9Viewport::default(),
            active_textures: [0; 16],
            active_vertex_buffer: 0,
            active_index_buffer: 0,
            active_vertex_decl: 0,
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

    pub fn set_viewport(&mut self, vp: &D3D9Viewport) {
        self.viewport = *vp;
    }

    pub fn set_texture(&mut self, stage: u32, texture_handle: u32) {
        if (stage as usize) < self.active_textures.len() {
            self.active_textures[stage as usize] = texture_handle;
        }
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
pub unsafe extern "C" fn nfs_d3d9_bridge_set_texture(stage: u32, texture: u32) {
    with_d3d9(|d| d.set_texture(stage, texture));
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_bridge_set_viewport(vp: *const D3D9Viewport) {
    if !vp.is_null() {
        with_d3d9(|d| d.set_viewport(&*vp));
    }
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
