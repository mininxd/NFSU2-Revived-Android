# Architecture Decision Records (ADRs) - NFSU2 Android 100% Rust Port

## ADR-001: 100% Rust Target Architecture
- **Status:** Accepted (2026-11-19)
- **Context:** The original app combines Java Android Activities/Views with C/C++ native code (Win32 emulation, recompilation host, Direct3D 9 to Vulkan bridge). The Java layer introduces GC pauses, memory pressure, and JNI transition overhead, while the C/C++ layer contains legacy manual memory and pointer risks.
- **Decision:** Migrate 100% of the application to Rust. The Android entry point will be driven directly by Android NDK (`android-activity` / `NativeActivity`), and all subsystems (network packet handling, guest emulation shims, Direct3D translation, audio and input) will be pure Rust crates.
- **Consequences:** Eliminates JVM Garbage Collection stuttering. Unifies the toolchain around Cargo. Enforces memory safety and eliminates undefined behavior.

## ADR-002: Embedded Rust Touch Controls and HUD
- **Status:** Accepted (2026-11-19)
- **Context:** `RacingControlsView.java` and `DiagnosticOverlay.java` currently run in the Android UI thread. In a pure Rust `NativeActivity`, the Android View hierarchy is bypassed.
- **Decision:** Render virtual steering, throttle, brake, nitro buttons, and HUD overlays directly in the native graphics pipeline (via an embedded lightweight HUD / `egui` pass) over the game backbuffer.
- **Consequences:** Removes the Android View system overhead. Zero-latency touch input polling directly synchronized with game simulation ticks.

## ADR-003: Stepwise Hybrid Migration
- **Status:** Accepted (2026-11-19)
- **Context:** Porting 300k+ lines in a single pass without verification risks long regression periods.
- **Decision:** Follow the `rust-anything` wave model. First, port high-frequency Java networking (`io.mininxd.nfsu2.net.*`) and C platform logging/affinity to Rust crates (`roam_rs`, `perf_cores`, `nfs_platform`). Gradually replace Java and C call sites, maintaining backward compatibility via JNI/C-ABI exports, before finally lifting to pure Rust.
- **Consequences:** The app remains buildable and testable at every intermediate commit.

## ADR-004: Direct Vulkan Backend using `ash`
- **Status:** Accepted (2026-11-19)
- **Context:** Direct3D 9 commands must be translated efficiently to modern mobile GPUs (Adreno / Mali) with lowest possible overhead and custom extension controls (e.g. `VK_KHR_portability_subset`, subpasses, memory types).
- **Decision:** Use `ash` for low-overhead, zero-abstraction Vulkan bindings in Rust rather than high-level middleware.
- **Consequences:** Direct control over GPU descriptor sets, pipeline caches, and command buffers matching the speed of C++.
