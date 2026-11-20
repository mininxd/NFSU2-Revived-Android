# NFSU2 Revived v1.0.beta1 (Pre-Release)

## What's New in v1.0.beta1

v1.0.beta1 marks the first beta release of the milestone 1.0 architecture, featuring a 100% native Direct3D 9 pipeline, major Rust engine migration across subsystems, unlocked all-core CPU scheduling, and a critical performance fix for modern flagship processors.

---

### Key Highlights & Changes

#### 1. 100% Native Direct3D 9 Translation Pipeline (DXVK Removed)
* Replaced the heavy DXVK translation layer with a lean, direct native Direct3D 9 engine.
* Native D3D9 COM interface directly targets Vulkan 1.3 without the translation memory footprint or descriptor overhead of generic desktop DXVK.
* Pipeline compilation and state management handled natively via the pure Rust `graphics_cache` engine.
* Reduced initial load times and eliminated random DXVK shader caching stalls.

#### 2. Enhanced Contrast Zero-Overhead Hardware Tone Mapping (Issue #70 Fix)
* Fixed severe framerate drops (~40 FPS) on Qualcomm Snapdragon 8 Elite (SM8850 / Adreno 840) and modern mobile GPUs when Enhanced Contrast was enabled.
* Permanently bypassed the expensive legacy D3D9 3-pass ping-pong `StretchRect` blit and CPU `LockRect` blur that stalled mobile TBDR tile ring buffers.
* Directly routed contrast state (`0x00876518`) to native hardware tone mapping shaders, preserving visual depth and color dynamics at a locked 60+ FPS.

#### 3. Unconstrained All-Cores CPU Affinity & Governor Scaling
* Completely removed artificial affinity masks that previously pinned game threads strictly to higher-numbered CPU cores.
* Fixed core idling on modern heterogeneous SoCs (such as Snapdragon 8 Elite's 2 Prime + 6 Performance Oryon cluster architecture) where cores 0–3 were misidentified as efficiency cores and left idle.
* Unlocked all available CPU cores for the kernel scheduler, allowing Qualcomm EAS (Energy Aware Scheduling) and WALT governors to dynamically scale clock frequencies to peak boost states.
* Configured real-time interactive thread priorities:
  * Main Render & Guest Emulation: `nice -20`
  * Guest Audio: `nice -16`
  * Background Workers / Disk I/O: `nice -10`
* Disabled sustained performance mode (`setSustainedPerformanceMode(false)`) to prevent thermal HAL frequency capping ceilings on HyperOS and modern Android 14+ devices.

#### 4. Expanded Pure Rust Subsystems
* **ADPF & Performance Tuner**: Migrated Android Dynamic Performance Framework thermal hints and thread role orchestration into native Rust.
* **Audio Watchdog**: Replaced legacy C audio monitoring with a pure Rust watchdog thread ensuring jitter-free AAudio streaming.
* **Online Routing & Settings**: Integrated pure Rust room routing, binary packet serialization, and atomic settings persistence.
* **Zero-Overhead Ring Buffer Logging**: High-throughput diagnostic logging with non-blocking circular buffers.

#### 5. Stability & Quality-of-Life Fixes
* Fixed game region detection in the launcher options by passing normalized absolute paths.
* Enhanced error reporting and diagnostic log packaging for troubleshooting on Android 14–17.

---

### Verification & Checksums
* **Application Version**: `1.0.beta1`
* **Version Code**: `93`
* **Architecture**: `arm64-v8a`
* **Target SDK**: Android 15 (API 35) / Min SDK: Android 9 (API 28)

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25...v1.0.beta1
