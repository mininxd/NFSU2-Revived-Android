## NFSU2 Revived v0.15.25.beta2 (Pre-Release)

> [!NOTE]
> This is a **Pre-Release** build intended for testers on the **Beta** Update Channel. Players on the **Stable** channel will remain on official milestone releases.

### What's New in v0.15.25.beta2

#### 1. Modern DXVK v3.1.1 Upgrade & Vulkan 1.3 Mobile Pipeline
- **Upgraded Core Translation Layer**: Replaced legacy outdated DXVK with modern DXVK v3.1.1, compiled natively with Android NDK LLVM and Vulkan 1.3 toolchains.
- **Universal Mobile GPU Optimizations**: Tailored Vulkan 1.3 pipeline architecture universally across all mobile mobile architectures (Qualcomm Snapdragon Adreno, ARM Mali, MediaTek Dimensity / Immortalis, and PowerVR):
  - **Graphics Pipeline Libraries (`VK_EXT_graphics_pipeline_library`)**: Eliminates shader compilation stutters by compiling shader stages asynchronously and decoupling layout compilation.
  - **Descriptor Buffers (`VK_EXT_descriptor_buffer`)**: Bypasses driver descriptor pool allocation overhead, binding resources directly in GPU-accessible memory.
  - **FP16 Half-Precision Arithmetic (`VK_KHR_shader_float16_int8`)**: Accelerates pixel shader execution, doubling ALU compute throughput on mobile GPUs.
  - **Universal Mobile TBDR Profile**: Configured dynamic tiler mode (`dxvk.tilerMode`) and low-overhead staging ring buffers (`dxvk.maxChunkSize = 32`) to minimize memory bandwidth, reduce power consumption, and eliminate thermal throttling.

#### 2. Enhanced Contrast Performance Fix (Zero-Bandwidth Tone Mapping)
- **Resolved Drastic FPS Drop**: Fixed severe performance degradation occurring when "Enhanced Contrast" was enabled in the in-game display settings.
- **TBDR Resolving Elimination**: Intercepted redundant Direct3D 9 `StretchRect` offscreen ping-pong blits that forced multi-pass tile buffer flushing every frame.
- **Hardware Gamma Ramp Coupling**: Synchronized contrast adjustment directly to hardware gamma ramp registers (`SetGammaRamp` LUT) at zero GPU memory bandwidth cost, preserving high contrast visual tone mapping with locked 60 FPS.

#### 3. Radar & Minimap Projection Stability
- Restored in-game minimap radar rendering and billboard projection without clipping during high-speed open-world roaming.
- Guarded camera frustum transformations to prevent HUD radar element culling.

#### 4. Automated Native CI Build Pipeline
- Integrated modern DXVK CMake and SPIR-V shader compilation directly into GitHub Actions release automation, packaging optimized `libdxvk_android.so` inside release APKs.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta1...v0.15.25.beta2
