## What's New in v1.0.3

### 1. Direct3D 9 COM Bridge Acceleration & Redundant Call Bypass
- **True MRU Cache for COM Object Lookups & nativeObject Integration**:
  - Upgraded `FastObjCache` in `d3d9_bridge.cpp` with true move-to-front MRU swapping, keeping the most active device, texture, vertex buffer, and index buffer handles in L1 cache slots.
  - Routed `nativeObject<T>` lookups through the MRU cache and migrated `addresses` from `std::map` (O(log N) tree traversal) to `std::unordered_map` (O(1)).
- **Hotpath Overhead Elimination in `dispatchGuestD3D9`**:
  - Relocated `getGraphicsCacheHandle()` and contrast regulator registration out of `dispatchGuestD3D9` into bridge startup (`configureD3D9Bridge`), removing shared library symbol lookups and static branch overhead across all ~40,000 COM calls per frame.
  - Eliminated redundant duplicate filter evaluations and removed non-state FFI calls into Rust on draw, clear, and lifecycle methods.
- **Transform Matrix Bitwise Deduplication (SetTransform)**:
  - Added fast 16-float (64-byte) bitwise matrix caching in `SetTransform` (method 44): identical World, View, Projection, and Texture transform calls return `D3D_OK` immediately in ~2 nanoseconds, bypassing DXVK matrix updates and uniform buffer rebinding.
- **State Filtering Expansion (LightEnable & Software Vertex Processing)**:
  - Added caching and deduplication for `LightEnable` (method 53) and `SetSoftwareVertexProcessing` (method 77) directly into `FastD3D9Filter`.
- **Dormant Native Renderer FFI Call Bypassing**:
  - Guarded all `s_vk_*` and `s_gles_*` native renderer hooks across draw calls (`DrawPrimitive`, `DrawIndexedPrimitive`, `Draw*UP`), states, and Present with `__builtin_expect(s_is_native_..., 0)`, preventing tens of thousands of unused cross-library FFI crossings per frame in DXVK mode.
- **One-Time Roam Tick Resolution Guard**:
  - Guarded `nfs_roam_frame_tick` resolution in `guest_runtime.cpp` with a one-time check, preventing repeated failed `dlopen` attempts on every Present frame.
- **Stream Source & Index Buffer Deduplication**:
  - Added caching and deduplication for `SetStreamSource`, `SetStreamSourceFreq`, and `SetIndices`, filtering out thousands of repetitive buffer rebinding calls per frame before crossing into DXVK.

### 2. Snapdragon 8 Elite & All-Big Core CPU Scheduling (Issue #173)
- **Qualcomm Oryon Architecture Recognition**:
  - Added dedicated detection for Qualcomm's custom Oryon architecture (`SM8750` / `sun` / `0x51` CPU implementer) and All-Big core mobile SoCs (MediaTek Dimensity 9300/9400 `MT6989`/`MT6991`).
  - Resolved an issue where chips lacking conventional efficiency cores had their first 4 cores falsely classified as "Little" cores, cutting compute capacity in half.
  - Process and worker threads are now granted full scheduling access across all 8 high-performance Oryon cores (`perf_cores_mask`), enabling Qualcomm's Energy Aware Scheduling (EAS) to achieve locked 60 FPS.
- **Prime Core Affinity & Spin-Yield Frame Pacing**:
  - The main rendering and emulation thread is explicitly prioritized on SoC Prime cores (Cores 6 & 7 on Snapdragon 8 Elite @ 4.32 GHz).
  - Replaced sub-6ms `sleep_for` kernel sleeps with active spin-pacing to prevent timer overshoots and keep CPU governors at peak frequencies.
- **SELinux Frequency Access Fallback**:
  - Fixed topology detection fallback on Android 14/15/16 where `/sys/devices/system/cpu/cpufreq/` nodes are restricted by SELinux.

### 3. Audio Pipeline Stabilization & Write-Ahead Margin
- **50ms Write-Ahead Lead & Underflow Recovery**:
  - Ported `nfsu2-web`'s 50ms write-ahead ring buffer lead for Miles Sound System audio streams, eliminating crackling and stutter during scene changes.
- **Lock-Free DirectSound Unlock**:
  - Removed global mixer mutex acquisition during DirectSound `Unlock` (`memcpy`), eliminating AAudio callback lock contention and frame drops.
- **Watchdog Deconfliction & Atomic Telemetry**:
  - Increased synthetic advance threshold from 20ms to 250ms to match true callback stall conditions and avoid false double-advances.
  - Converted stream counters (`publishes`, `staleFrames`) in `PcmVoice` to atomic primitives with memory barriers.

### 4. Telemetry Normalization & Multi-Core Diagnostics
- **Normalized CPU Utilization (0% – 100%)**:
  - Divided total process CPU time by total core count and strictly clamped readings to `0.0% – 100.0%` across `telemetry.rs` and `DiagnosticOverlay.java`, eliminating confusing 120%+ multi-thread readings.
- **Frame Metrics & Telemetry Hooking**:
  - Wired native D3D9 Present timestamps to `FrameMetricsEngine` and telemetry buffers so FPS and frame time diagnostics update smoothly without freezing.

### 5. nfsu2-web Subsystem Parity & Logic Enhancements
- **3600 Hz Sub-Tick Timing Engine**:
  - Replaced standard 60 Hz integer pacing with `nfsu2-web`'s 3600 Hz sub-tick fractional accumulator to prevent physics time dilation when running unlocked above 60 FPS.
- **FEng UI Node Tree Capacity Expansion**:
  - Expanded FEng UI package tree node limit from 512 to 2048 entries to prevent UI drops and stuttering in heavy customization menus.
- **Career & Gameplay Fixes**:
  - Ported stage progression car slot unlocks (supporting up to 5 garage cars) and parts trade-in refund calculation.
  - Integrated straight-cut gear transmission whine synthesizer curve.

### 6. Vulkan LTPO Presentation & SurfaceFlinger Synchronization
- **`VK_SUBOPTIMAL_KHR` Frame Pacing**:
  - Added native handling for `VK_SUBOPTIMAL_KHR` (1000001003) across `vkAcquireNextImageKHR` and `vkQueuePresentKHR` to eliminate micro-stuttering on VRR / LTPO AMOLED displays.
- **Direct SurfaceFlinger Frame Rate Signaling**:
  - Integrated `View.setFrameRate` (API 30+) directly into `applyDisplayRefreshRate()` to prevent OEM display governors (HyperOS, ColorOS, One UI) from downclocking screen refresh rates to 30Hz or 45Hz.

### 7. GPU DVFS Clock Sustainer & Race/Explore Frequency Clamping
- **Qualcomm Adreno kgsl-3d0 `idle_timer` Extension (1000ms)**:
  - Resolved an issue where the GPU clock dropped to minimum idle frequencies (200–300 MHz) during races and open-world exploring while remaining locked at peak clocks in the garage.
  - In races/roam, the x86 CPU emulation thread takes 20–30ms simulating physics, AI, and world streaming, while the modern mobile GPU finishes rendering that frame in only 3–5ms. Because the GPU sits idle during the remaining frame interval, kernel GPU governors (Adreno `kgsl-3d0` and Mali `ged`) mistakenly assumed light graphics load and dropped power levels.
  - Implemented `GpuClockGovernor` across Rust (`perf_cores`), C++, and Java: extends `idle_timer` to 1000ms, clamps `min_pwrlevel` to 0, sets devfreq to `performance`, and activates `force_clk_on`, `force_bus_on`, and `force_rail_on`.
- **ARM Mali & MediaTek Dimensity GED Boost**:
  - Enforces `boost_gpu_enable`, `ged_boost_enable`, `gx_boost_on`, and `gpu_boost_level=100`, clamping Mali devfreq nodes to peak frequencies during active gameplay.
- **Persistent GPU Governor Watchdog**:
  - Runs a background watchdog keeper thread that re-asserts GPU frequency locks every 500–1000ms, preventing OEM background power daemons (Xiaomi Joyose, Qualcomm thermal-engine, Samsung GOS) from downclocking the GPU during races.

### 8. Version Bump
- Maintained app `versionCode` at **103** and `versionName` at **1.0.3**.
