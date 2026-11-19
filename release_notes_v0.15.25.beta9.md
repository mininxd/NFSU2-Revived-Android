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

#### 2. Native ARM64 Direct Vulkan Engine (`graphics_cache::native_vk`)
- **Direct Native Vulkan Swapchain & Surface Architecture**:
  - Implemented direct `libvulkan.so` loader, `VkInstance`, `VkDevice`, and `VkSwapchainKHR` on Android `ANativeWindow`.
  - Turns DXVK into a lightweight pass-through/dummy COM wrapper while actual swapchain buffers, image acquisition, and presentation are executed natively by pure Rust.
  - Eliminates desktop Vulkan translation overhead while preserving full UI option compatibility ("Vulkan (DXVK)") ahead of the 1.0 zero-DXVK release.

```rust
// graphics_cache/src/native_vk.rs - Direct Native Vulkan Presentation
#[no_mangle]
pub unsafe extern "C" fn nfs_native_vk_present() -> c_int {
    let Some(ref api) = VK_API else { return -1; };
    let mut guard = VK_STATE.lock().unwrap();
    if let Some(ref mut ctx) = *guard {
        let mut image_index = 0u32;
        if (api.vk_acquire_next_image_khr)(ctx.device, ctx.swapchain, 1_000_000_000, 0, 0, &mut image_index) == VK_SUCCESS {
            let present_info = VkPresentInfoKHR {
                s_type: VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                swapchain_count: 1,
                p_swapchains: &ctx.swapchain,
                p_image_indices: &image_index,
                ..
            };
            (api.vk_queue_present_khr)(ctx.queue, &present_info);
            return 0;
        }
    }
    -1
}
```

#### 3. Draw Distance Internal Thresholds Extended (+100m Across All Presets)
- Enhanced world streaming and geometry horizon internally across all presets (keeping clean UI option names):
  - **Ultra Performance**: Extended to 175m (was 75m)
  - **Performance**: Extended to 275m (was 175m)
  - **Balanced**: Extended to 450m (was 350m)
  - **Extended**: Extended to 700m (was 600m)

#### 4. Build & Version Update
- Bumped app `versionCode` to **90** and `versionName` to **0.15.25.beta9**.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta8...v0.15.25.beta9
