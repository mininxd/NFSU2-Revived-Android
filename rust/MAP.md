# Migration Map - NFSU2 Android to 100% Pure Rust

- **Goal:** Standalone, pure Rust Android game application (`libnfsu2_android.so` / `NativeActivity`) with zero Java GC overhead and safe native systems.
- **Architecture Decisions:** Defined in [DECISIONS.md](DECISIONS.md)
- **Domain Terminology:** Defined in [GLOSSARY.md](GLOSSARY.md)

---

## Migration Waves

### Wave 0: Core Foundation & Platform Services (Active)
- [x] **ADRs & Architecture Map** (`rust/DECISIONS.md`, `rust/MAP.md`, `rust/GLOSSARY.md`)
- [x] **`src/platform/nfs_log.c` -> `rust/crates/nfs_platform`**: Safe C-ABI logging engine for recompilation host and Android.
- [x] **`android/app/src/main/cpp/roam/roam_affinity.c` -> `perf_cores`**: CPU cluster detection and thread pinning with C-ABI export `nfs_bind_to_perf_cores`.

### Wave 1: Multiplayer & Network Protocol Engine (Active)
- [x] **`VirtualSwitchClient.java` Packet Engine -> `roam_rs::switch`**:
  - RFC 6455 WebSocket frame encoding/decoding
  - Decapsulation for `KIND_UDP` (1), `KIND_LAN_UDP` (3), `KIND_TCP` (4), `KIND_BUNDLE` (32)
  - Zero-copy buffer serialization (`PacketWriter` / `PacketReader`)
  - JNI & C-ABI bindings allowing Java and native C code to bypass manual allocations.
- [ ] **`AdhocRoamClient.java` Peer Coordinator**: Migrate peer management and state synchronization to `roam_rs`.

### Wave 2: Win32 Emulation & Host Runtime
- [ ] **`src/shims/win32_shims.c` -> `graphics_cache::win32_*`**:
  - Guest module queries (`GetModuleHandleA`, `GetModuleFileNameA`)
  - File system mapping and stdcall dispatch
- [ ] **`guest_kernel.inc` & `guest_threads.inc`**: Memory mapping and thread pools in Rust.

### Wave 3: Input, Audio & UI
- [ ] **`RacingControlsView.java` -> `game_core::touch_hud`**: Native touch controls and HUD.
- [ ] **`audio_mixer.h` & `audio_watchdog.cpp` -> `graphics_cache::audio_engine`**: Audio resampling and playback.

### Wave 4: Graphics & Final Pure Lift
- [ ] **`d3d9_bridge.cpp` -> `ash` Vulkan Layer**: Direct3D 9 to Vulkan state translation in Rust.
- [ ] **Pure App Lift**: Switch Android entry point to `android-activity` (`NativeActivity`), removing the Java VM layer.
