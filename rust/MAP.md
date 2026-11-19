# Migration Map - NFSU2 Android to 100% Pure Rust

- **Goal:** Standalone, pure Rust Android game application (`libnfsu2_android.so` / `NativeActivity`) with zero Java GC overhead and safe native systems.
- **Architecture Decisions:** Defined in [DECISIONS.md](DECISIONS.md)
- **Domain Terminology:** Defined in [GLOSSARY.md](GLOSSARY.md)

---

## Migration Waves

### Wave 0: Core Foundation & Platform Services (Active)
- [x] **ADRs & Architecture Map** (`rust/DECISIONS.md`, `rust/MAP.md`, `rust/GLOSSARY.md`)
- [x] **`src/platform/nfs_log.c` -> `rust/crates/nfs_platform`**: Safe C-ABI logging engine for recompilation host and Android.
- [x] **`roam_affinity.c` & `performance_tuner.cpp` -> `perf_cores`**:
  - Tri-cluster CPU topology detection (Prime, Mid, Little cores) via cpufreq and `/proc/cpuinfo` Cortex part identification.
  - Role-based thread affinity and priority tuning (`RenderMain` -> Prime/Mid, `GuestAudio` -> Mid isolated, `BackgroundWorker` -> Little).
  - C-ABI (`nfs_perf_tune_thread_role`, `nfs_perf_get_topology_summary_buf`, `nfs_bind_to_perf_cores`) and JNI bindings.

### Wave 1: Multiplayer & Network Protocol Engine (Active)
- [x] **`VirtualSwitchClient.java` Packet Engine -> `roam_rs::switch`**:
  - RFC 6455 WebSocket frame encoding/decoding
  - Decapsulation for `KIND_UDP` (1), `KIND_LAN_UDP` (3), `KIND_TCP` (4), `KIND_BUNDLE` (32)
  - Zero-copy buffer serialization (`PacketWriter` / `PacketReader`)
- [x] **`AdhocPostofficeClient.java` Protocol Engine -> `roam_rs::postoffice`**:
  - Binary opcodes: `OPCODE_PING`, `OPCODE_LOGIN`, `OPCODE_CONNECT`, `OPCODE_DISCONNECT`, `OPCODE_CONNECT_BSSID`, `OPCODE_CHAT`
  - Postoffice PDP 24-byte registration and 14-byte datagram header parsing.
- [ ] **`AdhocRoamClient.java` Peer Coordinator**: Migrate peer management and state synchronization to `roam_rs`.

### Wave 2: Win32 Emulation & Host Runtime (Active)
- [x] **`src/shims/win32_shims.c` -> `graphics_cache::win32_shims`**:
  - Guest module queries (`GetModuleHandleA`, `GetModuleFileNameA`)
  - Guest PE RT_RCDATA resource directory traversal (`guest_find_resource`, `guest_load_resource`, `guest_sizeof_resource`)
  - Window frame style normalization (`filter_window_long`)
- [ ] **`guest_kernel.inc` & `guest_threads.inc`**: Memory mapping and thread pools in Rust.

### Wave 3: Input, Audio & UI (Active)
- [ ] **`RacingControlsView.java` -> `game_core::touch_hud`**: Native touch controls and HUD.
- [x] **`audio_watchdog.cpp` -> `graphics_cache::audio_watchdog`**: Safe atomic watchdog and synthetic DirectSound clock progression.
- [ ] **`audio_mixer.h` -> `graphics_cache::audio_engine`**: Full audio playback pipeline lift.

### Wave 4: Graphics & Final Pure Lift (Active)
- [x] **`vulkan_driver.cpp` -> `graphics_cache::vulkan_driver`**: Safe Vulkan driver opening and proc address resolution.
- [ ] **`d3d9_bridge.cpp` -> `ash` Vulkan Layer**: Direct3D 9 to Vulkan state translation in Rust.
- [ ] **Pure App Lift**: Switch Android entry point to `android-activity` (`NativeActivity`), removing the Java VM layer.
