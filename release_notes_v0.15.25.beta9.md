## NFSU2 Revived v0.15.25.beta9

### What's New in v0.15.25.beta9

#### 1. Native ARM64 Direct Mobile Graphics Engine (Pure Rust GLES)
- **Direct EGL Window Surface & Swapchain Presentation**:
  - Replaced DXVK Vulkan surface presentation with direct `libEGL.so` swapchain buffers via `eglSwapBuffers` and `ANativeWindow` lifecycle integration in `graphics_cache::native_gles`.
  - Eliminates desktop DXVK descriptor buffer and Graphics Pipeline Library (GPL) driver lockups on mobile tile-based architectures (Mali-G57, Mali-G610, PowerVR, Adreno).
- **Pure Rust OpenGL ES State Machine**:
  - Direct pipeline state dispatch for D3D9 operations:
    - Viewport & Scissor (`glViewport`, `glScissor`)
    - Framebuffer Clearing (`glClearColor`, `glClearDepthf`, `glClear`)
    - World / View / Projection 4x4 matrix transforms
    - Blend modes & Depth Stencil tests (`glBlendFuncSeparate`, `glDepthFunc`, `glDepthMask`)
    - Texture stage binding (`glActiveTexture`, `glBindTexture`)
- **Direct Draw Primitive & Indexed UP Pipeline**:
  - Direct zero-copy primitives dispatch for `DrawPrimitive` (`glDrawArrays`), `DrawIndexedPrimitive` (`glDrawElements`), and client-memory vertex buffers (`DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`).

```rust
// graphics_cache/src/native_gles.rs - Native Mobile Primitives Dispatch
#[no_mangle]
pub unsafe extern "C" fn nfs_native_gles_draw_indexed(
    prim_type: u32,
    _base_vertex_index: i32,
    _min_vertex_index: u32,
    _num_vertices: u32,
    start_index: u32,
    prim_count: u32,
) -> c_int {
    let Some(ref api) = GLES_API else { return -1; };
    let (mode, count) = match prim_type {
        4 => (GL_TRIANGLES, prim_count * 3),
        5 => (GL_TRIANGLE_STRIP, prim_count + 2),
        6 => (GL_TRIANGLE_FAN, prim_count + 2),
        2 => (GL_LINES, prim_count * 2),
        _ => return -2,
    };
    let offset = (start_index * 2) as usize;
    (api.gl_draw_elements)(mode, count as i32, GL_UNSIGNED_SHORT, offset as *const c_void);
    TOTAL_DRAWS.fetch_add(1, Ordering::Relaxed);
    0
}
```

#### 2. Draw Distance Setting Extended (+100m Across All Presets)
- Enhanced world streaming and geometry horizon across all presets:
  - **Ultra Performance**: Extended to **+175m** (was +75m)
  - **Performance**: Extended to **+275m** (was +175m)
  - **Balanced**: Extended to **+450m** (was +350m)
  - **Extended**: Extended to **+700m** (was +600m)

#### 3. Build & Version Update
- Bumped app `versionCode` to **90** and `versionName` to **0.15.25.beta9**.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta8...v0.15.25.beta9
