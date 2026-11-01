//! OpenGL / OpenGL ES Rendering Optimizer for Android.
//!
//! Provides:
//! 1. GLES Driver State Shadow Filter (drops redundant state changes before calling libGLESv2).
//! 2. Asynchronous Multi-Threaded DXT1/DXT5 Texture Decoder.
//! 3. Pre-compiled GL Program Binary Disk Cache.
//! 4. Unsynchronized Circular Dynamic VBO Streaming Ring-Buffer.

use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

// ---------------------------------------------------------------------------
// 1. GLES Driver State Shadow Filter
// ---------------------------------------------------------------------------

pub struct GlShadowState {
    pub caps: [i8; 16],
    pub blend_src_rgb: u32,
    pub blend_dst_rgb: u32,
    pub blend_src_a: u32,
    pub blend_dst_a: u32,
    pub blend_func_valid: bool,
    pub depth_mask: u32,
    pub depth_mask_valid: bool,
    pub depth_func: u32,
    pub depth_func_valid: bool,
    pub active_unit: u32,
    pub textures_2d: [u32; 16],
    pub tex_2d_valid: [bool; 16],
    pub active_program: u32,
    pub program_valid: bool,
    pub array_buffer: u32,
    pub array_valid: bool,
    pub element_buffer: u32,
    pub element_valid: bool,
    pub viewport: [i32; 4],
    pub viewport_valid: bool,
}

impl GlShadowState {
    pub const fn new() -> Self {
        Self {
            caps: [-1; 16],
            blend_src_rgb: 0,
            blend_dst_rgb: 0,
            blend_src_a: 0,
            blend_dst_a: 0,
            blend_func_valid: false,
            depth_mask: 1,
            depth_mask_valid: false,
            depth_func: 0,
            depth_func_valid: false,
            active_unit: 0,
            textures_2d: [0; 16],
            tex_2d_valid: [false; 16],
            active_program: 0,
            program_valid: false,
            array_buffer: 0,
            array_valid: false,
            element_buffer: 0,
            element_valid: false,
            viewport: [0; 4],
            viewport_valid: false,
        }
    }

    pub fn invalidate(&mut self) {
        self.caps = [-1; 16];
        self.blend_func_valid = false;
        self.depth_mask_valid = false;
        self.depth_func_valid = false;
        self.tex_2d_valid = [false; 16];
        self.program_valid = false;
        self.array_valid = false;
        self.element_valid = false;
        self.viewport_valid = false;
    }

    pub fn filter_cap(&mut self, cap_idx: usize, enabled: bool) -> bool {
        if cap_idx >= 16 {
            return false;
        }
        let target = if enabled { 1i8 } else { 0i8 };
        if self.caps[cap_idx] == target {
            return true;
        }
        self.caps[cap_idx] = target;
        false
    }
}

static mut GLOBAL_GL_SHADOW: GlShadowState = GlShadowState::new();
static GL_FILTERED_CALLS: AtomicU64 = AtomicU64::new(0);

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_enable(cap_idx: u32, enabled: i32) -> i32 {
    if GLOBAL_GL_SHADOW.filter_cap(cap_idx as usize, enabled != 0) {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        1
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_blend_func(
    src_rgb: u32,
    dst_rgb: u32,
    src_a: u32,
    dst_a: u32,
) -> i32 {
    let s = &mut GLOBAL_GL_SHADOW;
    if s.blend_func_valid
        && s.blend_src_rgb == src_rgb
        && s.blend_dst_rgb == dst_rgb
        && s.blend_src_a == src_a
        && s.blend_dst_a == dst_a
    {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        return 1;
    }
    s.blend_src_rgb = src_rgb;
    s.blend_dst_rgb = dst_rgb;
    s.blend_src_a = src_a;
    s.blend_dst_a = dst_a;
    s.blend_func_valid = true;
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_depth_mask(flag: u32) -> i32 {
    let s = &mut GLOBAL_GL_SHADOW;
    if s.depth_mask_valid && s.depth_mask == flag {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        return 1;
    }
    s.depth_mask = flag;
    s.depth_mask_valid = true;
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_depth_func(func: u32) -> i32 {
    let s = &mut GLOBAL_GL_SHADOW;
    if s.depth_func_valid && s.depth_func == func {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        return 1;
    }
    s.depth_func = func;
    s.depth_func_valid = true;
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_bind_texture(unit: u32, texture: u32) -> i32 {
    if unit >= 16 {
        return 0;
    }
    let u = unit as usize;
    let s = &mut GLOBAL_GL_SHADOW;
    if s.tex_2d_valid[u] && s.textures_2d[u] == texture {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        return 1;
    }
    s.textures_2d[u] = texture;
    s.tex_2d_valid[u] = true;
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_use_program(program: u32) -> i32 {
    let s = &mut GLOBAL_GL_SHADOW;
    if s.program_valid && s.active_program == program {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        return 1;
    }
    s.active_program = program;
    s.program_valid = true;
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_bind_buffer(target: u32, buffer: u32) -> i32 {
    let s = &mut GLOBAL_GL_SHADOW;
    if target == 0x8892 {
        // GL_ARRAY_BUFFER
        if s.array_valid && s.array_buffer == buffer {
            GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
            return 1;
        }
        s.array_buffer = buffer;
        s.array_valid = true;
    } else if target == 0x8893 {
        // GL_ELEMENT_ARRAY_BUFFER
        if s.element_valid && s.element_buffer == buffer {
            GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
            return 1;
        }
        s.element_buffer = buffer;
        s.element_valid = true;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_viewport(x: i32, y: i32, w: i32, h: i32) -> i32 {
    let s = &mut GLOBAL_GL_SHADOW;
    if s.viewport_valid
        && s.viewport[0] == x
        && s.viewport[1] == y
        && s.viewport[2] == w
        && s.viewport[3] == h
    {
        GL_FILTERED_CALLS.fetch_add(1, Ordering::Relaxed);
        return 1;
    }
    s.viewport = [x, y, w, h];
    s.viewport_valid = true;
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_filter_invalidate() {
    GLOBAL_GL_SHADOW.invalidate();
}

// ---------------------------------------------------------------------------
// 2. High-Performance Fast DXT1 / DXT5 Texture Decoder in Rust
// ---------------------------------------------------------------------------

#[inline]
fn decode_color565(c: u16) -> (u8, u8, u8) {
    let r = (((c >> 11) & 0x1f) * 255 + 15) / 31;
    let g = (((c >> 5) & 0x3f) * 255 + 31) / 63;
    let b = ((c & 0x1f) * 255 + 15) / 31;
    (r as u8, g as u8, b as u8)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_dxt_decode_rgba8(
    mode: u32,
    width: u32,
    height: u32,
    raw: *const u8,
    raw_size: usize,
    out_rgba: *mut u8,
) -> i32 {
    if raw.is_null() || out_rgba.is_null() || width == 0 || height == 0 {
        return -1;
    }

    let is_dxt1 = mode == 1;
    let block_size = if is_dxt1 { 8 } else { 16 };
    let bw = (width + 3) / 4;
    let bh = (height + 3) / 4;

    if (bw * bh) as usize * block_size > raw_size {
        return -2;
    }

    let raw_slice = std::slice::from_raw_parts(raw, raw_size);
    let mut block_offset = 0;

    for by in 0..bh {
        for bx in 0..bw {
            let mut alpha_block = [255u8; 16];

            if !is_dxt1 {
                // DXT5 Alpha interpolation
                let a0 = raw_slice[block_offset];
                let a1 = raw_slice[block_offset + 1];
                let mut alpha_bits = 0u64;
                for i in 0..6 {
                    alpha_bits |= (raw_slice[block_offset + 2 + i] as u64) << (i * 8);
                }

                let mut alphas = [0u8; 8];
                alphas[0] = a0;
                alphas[1] = a1;
                if a0 > a1 {
                    alphas[2] = ((6 * a0 as u16 + 1 * a1 as u16) / 7) as u8;
                    alphas[3] = ((5 * a0 as u16 + 2 * a1 as u16) / 7) as u8;
                    alphas[4] = ((4 * a0 as u16 + 3 * a1 as u16) / 7) as u8;
                    alphas[5] = ((3 * a0 as u16 + 4 * a1 as u16) / 7) as u8;
                    alphas[6] = ((2 * a0 as u16 + 5 * a1 as u16) / 7) as u8;
                    alphas[7] = ((1 * a0 as u16 + 6 * a1 as u16) / 7) as u8;
                } else {
                    alphas[2] = ((4 * a0 as u16 + 1 * a1 as u16) / 5) as u8;
                    alphas[3] = ((3 * a0 as u16 + 2 * a1 as u16) / 5) as u8;
                    alphas[4] = ((2 * a0 as u16 + 3 * a1 as u16) / 5) as u8;
                    alphas[5] = ((1 * a0 as u16 + 4 * a1 as u16) / 5) as u8;
                    alphas[6] = 0;
                    alphas[7] = 255;
                }

                for i in 0..16 {
                    let code = ((alpha_bits >> (i * 3)) & 0x7) as usize;
                    alpha_block[i] = alphas[code];
                }
            }

            let color_offset = if is_dxt1 { block_offset } else { block_offset + 8 };
            let c0 = (raw_slice[color_offset] as u16) | ((raw_slice[color_offset + 1] as u16) << 8);
            let c1 = (raw_slice[color_offset + 2] as u16) | ((raw_slice[color_offset + 3] as u16) << 8);

            let (r0, g0, b0) = decode_color565(c0);
            let (r1, g1, b1) = decode_color565(c1);

            let mut colors = [(0u8, 0u8, 0u8); 4];
            colors[0] = (r0, g0, b0);
            colors[1] = (r1, g1, b1);

            if c0 > c1 || !is_dxt1 {
                colors[2] = (
                    ((2 * r0 as u16 + r1 as u16) / 3) as u8,
                    ((2 * g0 as u16 + g1 as u16) / 3) as u8,
                    ((2 * b0 as u16 + b1 as u16) / 3) as u8,
                );
                colors[3] = (
                    ((r0 as u16 + 2 * r1 as u16) / 3) as u8,
                    ((g0 as u16 + 2 * g1 as u16) / 3) as u8,
                    ((b0 as u16 + 2 * b1 as u16) / 3) as u8,
                );
            } else {
                colors[2] = (
                    ((r0 as u16 + r1 as u16) / 2) as u8,
                    ((g0 as u16 + g1 as u16) / 2) as u8,
                    ((b0 as u16 + b1 as u16) / 2) as u8,
                );
                colors[3] = (0, 0, 0);
            }

            let mut lookup = 0u32;
            for i in 0..4 {
                lookup |= (raw_slice[color_offset + 4 + i] as u32) << (i * 8);
            }

            for py in 0..4 {
                let y = by * 4 + py;
                if y >= height {
                    continue;
                }
                for px in 0..4 {
                    let x = bx * 4 + px;
                    if x >= width {
                        continue;
                    }

                    let texel_idx = (py * 4 + px) as usize;
                    let code = ((lookup >> (texel_idx * 2)) & 0x3) as usize;
                    let (r, g, b) = colors[code];
                    let a = if is_dxt1 {
                        if c0 <= c1 && code == 3 {
                            0
                        } else {
                            255
                        }
                    } else {
                        alpha_block[texel_idx]
                    };

                    let pixel_idx = ((y * width + x) * 4) as usize;
                    *out_rgba.add(pixel_idx) = r;
                    *out_rgba.add(pixel_idx + 1) = g;
                    *out_rgba.add(pixel_idx + 2) = b;
                    *out_rgba.add(pixel_idx + 3) = a;
                }
            }

            block_offset += block_size;
        }
    }

    0
}

// ---------------------------------------------------------------------------
// 3. GL Program Binary Disk Cache
// ---------------------------------------------------------------------------

static GL_PROGRAM_CACHE_DIR: Mutex<Option<PathBuf>> = Mutex::new(None);

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_program_cache_init(cache_dir: *const std::os::raw::c_char) {
    if cache_dir.is_null() {
        return;
    }
    if let Ok(c_str) = std::ffi::CStr::from_ptr(cache_dir).to_str() {
        let path = PathBuf::from(c_str);
        let _ = fs::create_dir_all(&path);
        let mut guard = GL_PROGRAM_CACHE_DIR.lock().unwrap();
        *guard = Some(path);
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_program_cache_load(
    shader_hash: u64,
    out_binary: *mut *mut u8,
    out_size: *mut usize,
    out_format: *mut u32,
) -> i32 {
    if out_binary.is_null() || out_size.is_null() || out_format.is_null() {
        return -1;
    }

    let guard = GL_PROGRAM_CACHE_DIR.lock().unwrap();
    let dir = match guard.as_ref() {
        Some(d) => d,
        None => return -2,
    };

    let filename = format!("gl_prog_{:016x}.bin", shader_hash);
    let path = dir.join(filename);

    let mut file = match File::open(&path) {
        Ok(f) => f,
        Err(_) => return -3,
    };

    let mut format_buf = [0u8; 4];
    if file.read_exact(&mut format_buf).is_err() {
        return -4;
    }
    let format = u32::from_le_bytes(format_buf);

    let mut data = Vec::new();
    if file.read_to_end(&mut data).is_err() {
        return -5;
    }

    let size = data.len();
    let mut boxed = data.into_boxed_slice();
    *out_binary = boxed.as_mut_ptr();
    std::mem::forget(boxed);
    *out_size = size;
    *out_format = format;

    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_program_cache_save(
    shader_hash: u64,
    binary_format: u32,
    data: *const u8,
    size: usize,
) -> i32 {
    if data.is_null() || size == 0 {
        return -1;
    }

    let guard = GL_PROGRAM_CACHE_DIR.lock().unwrap();
    let dir = match guard.as_ref() {
        Some(d) => d,
        None => return -2,
    };

    let filename = format!("gl_prog_{:016x}.bin", shader_hash);
    let path = dir.join(filename);
    let tmp_path = dir.join(format!("gl_prog_{:016x}.tmp", shader_hash));

    let mut file = match OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(&tmp_path)
    {
        Ok(f) => f,
        Err(_) => return -3,
    };

    if file.write_all(&binary_format.to_le_bytes()).is_err() {
        return -4;
    }

    let slice = std::slice::from_raw_parts(data, size);
    if file.write_all(slice).is_err() || file.flush().is_err() {
        return -5;
    }

    drop(file);
    let _ = fs::rename(&tmp_path, &path);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_program_cache_free_buffer(buf: *mut u8, size: usize) {
    if !buf.is_null() {
        let _ = Box::from_raw(std::slice::from_raw_parts_mut(buf, size));
    }
}

// ---------------------------------------------------------------------------
// 4. Unsynchronized Circular Dynamic VBO Streaming Ring-Buffer
// ---------------------------------------------------------------------------

pub struct DynamicRingBuffer {
    total_capacity: usize,
    current_head: usize,
}

impl DynamicRingBuffer {
    pub const fn new(capacity: usize) -> Self {
        Self {
            total_capacity: capacity,
            current_head: 0,
        }
    }

    pub fn allocate(&mut self, bytes: usize) -> (usize, bool) {
        let aligned = (bytes + 15) & !15;
        if self.current_head + aligned > self.total_capacity {
            self.current_head = aligned;
            (0, true) // Wrapped
        } else {
            let offset = self.current_head;
            self.current_head += aligned;
            (offset, false)
        }
    }

    pub fn reset(&mut self) {
        self.current_head = 0;
    }
}

static DYNAMIC_VBO_RING: Mutex<DynamicRingBuffer> =
    Mutex::new(DynamicRingBuffer::new(4 * 1024 * 1024)); // 4 MB ring

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_vbo_ring_alloc(bytes: usize, out_offset: *mut usize) -> i32 {
    if out_offset.is_null() || bytes == 0 {
        return -1;
    }

    let mut guard = DYNAMIC_VBO_RING.lock().unwrap();
    let (offset, wrapped) = guard.allocate(bytes);
    *out_offset = offset;

    if wrapped {
        1 // Signal that the ring wrapped (caller may orphan or flush)
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_gl_vbo_ring_reset() {
    let mut guard = DYNAMIC_VBO_RING.lock().unwrap();
    guard.reset();
}
