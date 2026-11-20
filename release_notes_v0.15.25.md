# NFSU2 Revived v0.15.25 — Official Release

## Overview of the v0.15.25 Release Cycle

The v0.15.25 release cycle represents a comprehensive overhaul of the Android port. Across eight beta releases and the final release milestone, this update delivers:
- Modern DXVK v3.1.1 and Vulkan 1.3 mobile translation.
- Pure Rust systems migration across networking, input, culling, audio, and scheduler components.
- Zero-bandwidth hardware tone reproduction and GPU frustum culling.
- Fluid touch HUD controls with non-linear S-curve steering and auto-dimming.
- Complete fixes for minimap/world map road rendering, showroom camera navigation, and stability.

---

## Full Beta Changelog: Beta 1 to Beta 8

### v0.15.25.beta1 — GPU Telemetry & Hardware Monitor
* GPU Utilization Detection: Resolved missing GPU metric in the diagnostic telemetry overlay ("Display Logs"). Added sysfs hardware node scanning across Qualcomm Snapdragon Adreno (kgsl-3d0), ARM Mali (utilization, gpu_busy), and MediaTek GED.
* Compositor FrameMetrics Fallback: Added Android FrameMetrics.GPU_DURATION (API 26+) dynamic workload tracking for locked-down devices where SELinux restricts direct sysfs access.
* Compact HUD Integration: Updated the on-screen performance HUD to display real-time GPU load (GPU: %) alongside CPU, RAM, and FPS metrics.

---

### v0.15.25.beta2 — Modern DXVK v3.1.1 & Zero-Bandwidth Contrast
* Modern DXVK v3.1.1 Engine: Upgraded the Direct3D 9 translation pipeline to modern DXVK v3.1.1 compiled with LLVM Android NDK and Vulkan 1.3.
* Vulkan 1.3 Mobile Features:
  * VK_EXT_graphics_pipeline_library: Pre-compiles pipeline stages asynchronously to eliminate mid-race shader stutter.
  * VK_EXT_descriptor_buffer: Bypasses driver descriptor pool allocation overhead.
  * VK_KHR_shader_float16_int8: Enforces FP16 half-precision arithmetic, doubling ALU throughput on mobile GPUs.
  * Universal TBDR profile with low-overhead chunk staging buffers (dxvk.maxChunkSize = 32).
* Enhanced Contrast Zero-Bandwidth Tone Mapping: Eliminated severe FPS drops with Enhanced Contrast enabled by replacing heavy StretchRect ping-pong render target blits with direct hardware gamma ramp LUT (SetGammaRamp) adjustments at locked 60 FPS.
* Minimap Radar Stability: Restored radar projection and billboard transformations during high-speed open-world free roam.
* Automated CI Packaging: Built DXVK and SPIR-V compilation directly into GitHub Actions release automation.

---

### v0.15.25.beta3 — Draw Distance & GPU Frustum Clipping
* Unified 3D Scene View FarZ Management: Harmonized draw distance clamping across all 3D passes (primary chase camera, secondary views, road reflections, and distant scenery).
* Direct3D 9 Hardware Rasterizer Far-Plane Culling: Enforced perspective projection far-plane clamping in SetTransform(D3DTS_PROJECTION) to physically discard distant geometry via hardware rasterization.
* PowerVR Tile Alignment: Configured 16x16 / 32x32 tile alignment on PowerVR Rogue GPUs.
* Adreno 740 / Turnip T30 Stability: Disabled problematic driver extensions causing tile buffer overflows on Snapdragon 8 Gen 2 / Turnip drivers.
* Diagnostic Bundle Auto-Collector: Integrated one-tap ZIP crash log and system report exporter.

---

### v0.15.25.beta4 — Dynamic Camera Culling & Fast-Forward I/O
* Pure Rust Dynamic Camera Back-Culling Engine (camera_cull):
  * Evaluates 3D world geometry dynamically against the active camera.
  * Automatically discards draw calls for geometry >50m behind the camera plane or beyond the draw horizon, cutting vertex shading and overdraw.
  * Dynamic look-back awareness: automatically inverts the culling boundary when glancing backwards.
* Fast-Forward Asynchronous Disk I/O: Streamed world chunk asset loading (STREAML5R.BUN) to eliminate stutters when crossing district boundaries (City Core, Beacon Hill, Jackson Heights).
* Native Thread Priority Scheduling: Elevated render and translation threads to real-time priority (SCHED_FIFO / nice -16).

---

### v0.15.25.beta5 — Real-Time Telemetry & Analog Right-Stick Orbit
* Real-Time On-Screen Telemetry Overlay: Displays rolling FPS, bridge latency (ms/frame), memory usage, and thermal indicators.
* Right-Stick Camera Orbit Engine (camera_stick): Recreated PS2-style analog 360-degree camera orbit for USB/Bluetooth gamepads.
* Non-Linear Steering Sensitivity: Dynamic speed-sensitive steering curve that tightens responsiveness at high velocities and relaxes it at low speeds.
* Memory-Safe String Sanitization: Guarded user profile names and save paths against memory overrun in guest C strings.

---

### v0.15.25.beta6 — Native DirectInput 8 Force Feedback & Haptics
* Pure Rust Force Feedback Synthesizer (win32_dinput): Replaced stubbed DirectInput COM methods with a complete physics-to-haptics synthesis engine in Rust.
* Haptic Waveform Translation: Translates guest DirectInput rumble effects (constant force, periodic sine/square, ramp, spring, and friction) into native Android vibration waveforms (Vibrator / VibratorManager).
* Camera Orbit Damping: Added smooth inertia decay and return-to-center physics when releasing the right thumbstick.
* Touch Layout Editor: Added interactive canvas touch button drag-and-drop repositioning with multi-slot profile saving.

---

### v0.15.25.beta7 — D3D9 FVF Stream Filter & Adaptive Contrast
* Pure Rust FVF Stream Decomposition (d3d9_filter): Parses Flexible Vertex Formats (XYZ, XYZRHW, Normal, Diffuse, TexCoord) in pure Rust without intermediate JNI hops.
* Adaptive Contrast Regulator (contrast_regulator): Dynamically measures scene luminance and modulates bleach bypass / Lightroom contrast to prevent blown-out neon reflections in wet streets.
* Camera Orbit Customization: Added vertical pitch limits and an Invert-Y camera toggle.
* Gamepad Mapping Fixes: Resolved button indexing conflicts on Sony DualSense and Xbox Wireless controllers.

---

### v0.15.25.beta8 — Rust Networking, Tri-Cluster Scheduler & DXVK Tuner
* Pure Rust Networking Engine (roam_rs):
  * Replaced C network bridge with a zero-copy packet switch for UDP, TCP, and Postoffice PDP protocol datagrams.
* Tri-Cluster CPU Scheduler (perf_cores):
  * Detects Prime (Cortex-X), Mid (Cortex-A78), and Little (Cortex-A55) mobile CPU topologies via /proc/cpuinfo and cpufreq.
  * Pins main render threads to Prime performance cores, audio workers to Mid cores, and async disk I/O to Little cores.
* Pure Rust Win32 Emulation Shims (win32_shims): Built memory-safe PE resource directory traversal (guest_find_resource) for D3DX shaders and effect loading.
* Adaptive Driver Profile Generator (dxvk_tuner): Automatically optimizes DXVK pipeline and memory caching parameters based on active GPU driver signatures.

---

## Key Changes in this Final Version (v0.15.25)

### 1. Minimap & World Map Road Network Restoration
* Fixed Invisible Road Mesh: Fixed the bug where the HUD circular minimap and fullscreen world map (M key) rendered blips and icons but showed no roads.
* Native Vulkan Interception Fix: Updated Rust native Vulkan draw and state stubs (nfs_native_vk_draw_*, set_viewport, etc.) to return -1 when native Vulkan is not active, preventing road mesh draw calls from being discarded before reaching DXVK.
* Sub-Viewport & Top-Down Camera Bypass: Updated CameraCullEngine to detect sub-viewports (minimap radar) and top-down camera angles, completely bypassing culling for radar passes and world map views.
* Eliminated Projection Far-Plane Clamping: Bypassed projection far-plane clamping in SetTransform(D3DTS_PROJECTION) for top-down, ortho, and sub-viewport passes so high-altitude camera views (1500m+) can see ground roads.

### 2. Touch Button Opacity Propagation Fix
* Full Text & Label Opacity Control: Fixed an issue where the text on touch buttons ("GAS", "BRAKE", "START", "OK", "BACK") stayed 100% solid white even when the opacity slider was lowered.
* Alpha Modulation in glyph(): Updated RacingControlsView to propagate effectiveOpacity into glyph() and apply alpha modulation across all text labels, nitro indicators, and handbrake text.

### 3. Garage & Showroom 3D Interaction Redesign
* Single-Finger Hold Gesture: Implemented a 100ms hold threshold to cleanly distinguish between tapping a UI button and dragging to rotate the vehicle in the 3D garage/showroom.
* Visual Affordances:
  * Replaced the bulky orbit ring with a clean, compact solid indicator at 25% opacity.
  * Added a smooth drag trailing effect behind the touch contact point.
  * Added an expanding ripple feedback animation on single tap or mouse click.
  * Removed unintuitive pinch-to-zoom and double-tap gestures to prevent accidental camera snap.

### 4. S-Curve Steering & Auto-Dimming Controls
* Non-Linear S-Curve Steering: Added progressive S-curve steering response for touch and analog inputs, providing refined micro-adjustments on straights and responsive turn-in during tight corners.
* Auto-Dimming Touch HUD: Controls smoothly fade to a subtle opacity during uninterrupted straight-line driving or idle moments, restoring full brightness instantly upon touch.

### 5. Final Java-to-Rust Migration Waves & Platform Cleanups
* Complete Memory-Safe Core: Completed migration of Waves 1–4 modules (touch_hud, guest_kernel, guest_threads, audio_engine, d3d9_bridge, and peer coordinator) to pure Rust with zero GC pause overhead.
* System Vulkan Driver & Storage Discovery: Cleaned up system Vulkan driver fallback paths and removed noisy storage auto-discovery loops.
