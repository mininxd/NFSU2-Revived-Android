### What's New in v1.0

> **Major Milestone: Transition to Direct Vulkan & OpenGL ES Pipeline (DXVK Removed)**
>
> Starting with **v1.0**, **NFSU2 Revived no longer uses DXVK**. 
>
> The desktop DXVK translation layer has been completely eliminated in favor of a Direct3D 9 execution pipeline designed for Android and mobile GPUs. Rendering commands now dispatch directly to Vulkan 1.3 and OpenGL ES through the pure Rust `graphics_cache` engine. This eliminates desktop DXVK descriptor overhead, removes translation-induced shader stutters during races, significantly lowers RAM consumption, and speeds up game startup times.

---

#### 1. Direct3D 9 Rendering Pipeline (DXVK Removed)
- **Elimination of DXVK Translation Overhead**:
  - Completely stripped the legacy desktop `dxvk_android` and `dxvk_modules` translation layers from the codebase.
  - Implemented a lean Direct3D 9 COM interface (`native_d3d9.cpp`) that routes directly to Vulkan 1.3 and GLES state machines in pure Rust.
  - Replaced generic desktop descriptor pools and shader translation caches with direct mobile memory management, drastically cutting RAM overhead and eliminating shader compilation hitching during races.
- **Pure Rust Graphics Pipeline State (`graphics_cache`)**:
  - Draw primitive dispatch, viewport management, depth/stencil state, and blend equations are now managed entirely by `graphics_cache`.
  - Native vertex and index buffer staging directly feeds mobile tile-based GPUs without intermediate conversion layers.

```cpp
// native_d3d9.cpp - Native Direct3D 9 Interface
HRESULT STDMETHODCALLTYPE NativeDirect3DDevice9::DrawIndexedPrimitive(
    D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex,
    UINT NumVertices, UINT startIndex, UINT primCount) {
    if (s_use_gles) {
        return nfs_native_gles_draw_indexed(Type, BaseVertexIndex, MinVertexIndex,
                                           NumVertices, startIndex, primCount) == 0 ? D3D_OK : E_FAIL;
    }
    return nfs_native_vk_draw_indexed(Type, BaseVertexIndex, MinVertexIndex,
                                     NumVertices, startIndex, primCount) == 0 ? D3D_OK : E_FAIL;
}
```

---

#### 2. Enhanced Contrast Hardware Tone Mapping & Tile Resolve Bypass
- **Elimination of TBDR Tile Stalls from Post-Processing**:
  - In the original PC engine, Enhanced Contrast relied on an unoptimized 3-pass ping-pong `StretchRect` resolve and CPU `LockRect` blur at the end of each frame (`sub_005caad0`). On mobile Tile-Based Deferred Rendering (TBDR) GPUs (e.g. Adreno, Mali, Immortalis), this forced repeated tile flushes and ring buffer syncs back to system memory.
  - Resolved an issue where `g_BleachByPassEnable` (`0x00870D00`) was being restored after render hooks, causing the engine to execute the heavy multi-pass resolve even when hardware tone mapping was active.
  - Engine post-processing flags are now permanently clamped to 0 while user settings are read directly from `0x00876518`, routing tone reproduction straight into zero-overhead hardware vertex/fragment shaders without breaking tile memory.

```c
// roam_android.c - Clamp Resolve Flags and Route Tone Mapping
static void hook_sub_005caad0(void) {
    if (do_bypass) {
        uint32_t master_contrast = 0;
        if (rh_mem_committed(0x00876518u, 4)) {
            master_contrast = MR32(0x00876518u);
        }
        if (rh_mem_committed(0x00870D00u, 4)) {
            uint32_t user_contrast = MR32(0x00870D00u) | master_contrast;
            if (s_set_contrast_active) {
                s_set_contrast_active(user_contrast);
            }
            MR32(0x00870D00u) = 0; // Prevent engine 3-pass StretchRect blit
        }
        if (rh_mem_committed(0x00870CFCu, 4)) {
            MR32(0x00870CFCu) = 0;
        }
        if (rh_mem_committed(0x00876514u, 4)) {
            MR32(0x00876514u) = 0;
        }
    }
    if (s_orig_5caad0) {
        s_orig_5caad0();
    }
    if (do_bypass) {
        // Keep engine flags clamped so SPEED2.EXE never triggers tile resolve stalls
        if (rh_mem_committed(0x00870D00u, 4)) MR32(0x00870D00u) = 0;
        if (rh_mem_committed(0x00870CFCu, 4)) MR32(0x00870CFCu) = 0;
    }
}
```

---

#### 3. Unconstrained All-Cores CPU Scheduling & Dynamic Governor Boost
- **Heterogeneous Multi-Cluster CPU Support**:
  - Removed restrictive CPU affinity masks that previously partitioned threads exclusively to high-numbered CPU IDs.
  - On modern heterogeneous architectures without conventional efficiency cores—such as Qualcomm Snapdragon 8 Elite (2 Prime Oryon + 6 Performance Oryon cores)—threads are no longer artificially excluded from cores 0–3, preventing core idling and allowing the kernel's Energy Aware Scheduling (EAS) and WALT governors to distribute load freely.
- **Priority-Driven Scheduling**:
  - Retained strict priority hierarchies while unconstraining core placement:
    - Main Render & Guest Emulation: `nice -20` (maximum priority, preventing micro-stutters)
    - Guest Audio: `nice -16` (guaranteed buffer delivery without underruns)
    - Background Tasks / Asset Streaming: `nice -10` (fast chunk streaming without preempting rendering)
- **Governor Frequency Uncapping**:
  - Disabled Android sustained performance mode (`setSustainedPerformanceMode(false)`), removing thermal HAL frequency clamps that artificially capped CPU/GPU clock boost ceilings.

```rust
// perf_cores/src/lib.rs - Unconstrained All-Core Affinity
pub fn apply_affinity(&self) -> Result<(), i32> {
    let all_cores_mask = if self.total_cores >= 64 {
        u64::MAX
    } else {
        (1u64 << self.total_cores) - 1
    };

    unsafe {
        // Give process and main render thread full access across all cores
        let mask_val = all_cores_mask as c_long;
        let _ = libc::syscall(
            libc::SYS_sched_setaffinity,
            libc::getpid() as c_int,
            std::mem::size_of::<c_long>(),
            &mask_val as *const c_long,
        );
        let _ = libc::setpriority(libc::PRIO_PROCESS, 0, -20);
        ...
    }
}
```

---

#### 4. Rust Engine Subsystem Migration
- **Pure Rust ADPF & Performance Tuner**: Replaced legacy C performance tuning with safe Rust thread role management and Android Dynamic Performance Framework thermal telemetry.
- **Audio Watchdog**: Dedicated Rust watchdog monitoring real-time AAudio stream health and recovery.
- **Online Routing & Settings Engine**: Pure Rust binary packet serialization, room routing, and configuration storage.
- **Zero-Overhead Ring Buffer Logging**: Lock-free circular memory log buffer for high-speed diagnostic telemetry without I/O blocking.

---

#### 5. Stability & Launcher Fixes
- **Normalized Region Detection**: Resolved path resolution issue in launcher settings where game region auto-detection failed when passed relative directory strings.
- **Diagnostic Log Packaging**: Integrated full platform and thread topology dumps into the diagnostic ZIP collector for faster troubleshooting.

---

### Build Information
* **Version**: `1.0`
* **Version Code**: `100`
* **Architecture**: `arm64-v8a`
* **Target SDK**: Android 15 (API 35) / Min SDK: Android 9 (API 28)

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25...v1.0
