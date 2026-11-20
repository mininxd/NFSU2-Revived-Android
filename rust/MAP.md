# Migration Map - NFSU2 Android to 100% Pure Rust

- **Goal:** Standalone, pure Rust Android game application (`libnfsu2_android.so` / `NativeActivity`) with zero Java GC overhead and safe native systems.
- **Architecture Decisions:** Defined in [DECISIONS.md](DECISIONS.md)
- **Domain Terminology:** Defined in [GLOSSARY.md](GLOSSARY.md)

---

## Migration Waves

### Wave 0: Core Foundation & Platform Services (Completed)
- [x] **ADRs & Architecture Map** (`rust/DECISIONS.md`, `rust/MAP.md`, `rust/GLOSSARY.md`)
- [x] **`src/platform/nfs_log.c` -> `rust/crates/nfs_platform`**: Safe C-ABI logging engine for recompilation host and Android.
- [x] **`roam_affinity.c` & `performance_tuner.cpp` -> `perf_cores`**:
  - Tri-cluster CPU topology detection (Prime, Mid, Little cores) via cpufreq and `/proc/cpuinfo` Cortex part identification.
  - Role-based thread affinity and priority tuning (`RenderMain` -> Prime/Mid, `GuestAudio` -> Mid isolated, `BackgroundWorker` -> Little).
  - C-ABI (`nfs_perf_tune_thread_role`, `nfs_perf_get_topology_summary_buf`, `nfs_bind_to_perf_cores`) and JNI bindings.

### Wave 1: Multiplayer & Network Protocol Engine (Completed)
- [x] **`VirtualSwitchClient.java` Packet Engine -> `roam_rs::switch`**:
  - RFC 6455 WebSocket frame encoding/decoding
  - Decapsulation for `KIND_UDP` (1), `KIND_LAN_UDP` (3), `KIND_TCP` (4), `KIND_BUNDLE` (32)
  - Zero-copy buffer serialization (`PacketWriter` / `PacketReader`)
- [x] **`AdhocPostofficeClient.java` Protocol Engine -> `roam_rs::postoffice`**:
  - Binary opcodes: `OPCODE_PING`, `OPCODE_LOGIN`, `OPCODE_CONNECT`, `OPCODE_DISCONNECT`, `OPCODE_CONNECT_BSSID`, `OPCODE_CHAT`
  - Postoffice PDP 24-byte registration and 14-byte datagram header parsing.
- [x] **`AdhocRoamClient.java` Peer Coordinator -> `roam_rs::peer`**:
  - Thread-safe native peer registry (`PeerRegistry`) and connection state machine (`RoamCoordinator`)
  - Virtual MAC generation, validation, and zero-allocation string parsing/formatting
  - Dynamic MAC-to-IPv4 routing with automatic Prometheus adhoc synthetic IP fallback
  - Automatic heartbeat stale peer pruning and C-ABI export bindings (`nfs_roam_peer_*`)

### Wave 2: Win32 Emulation & Host Runtime (Completed)
- [x] **`src/shims/win32_shims.c` -> `graphics_cache::win32_shims`**:
  - Guest module queries (`GetModuleHandleA`, `GetModuleFileNameA`)
  - Guest PE RT_RCDATA resource directory traversal (`guest_find_resource`, `guest_load_resource`, `guest_sizeof_resource`)
  - Window frame style normalization (`filter_window_long`)
- [x] **`guest_kernel.inc` -> `graphics_cache::guest_kernel`**:
  - Thread-Local Storage (TLS) manager with 1088 slots (`tls_alloc`, `tls_free`, `tls_get_value`, `tls_set_value`)
  - Critical section registry with recursive locks, spin count, and deadlock cycle detection / breaking
  - ANSI and UTF-16 guest string decoding
- [x] **`guest_threads.inc` -> `graphics_cache::guest_threads`**:
  - Guest Mutex (`GuestMutex`) with owner thread tracking and recursion depth
  - Guest Event (`GuestEvent`) with manual/auto-reset and generation counters
  - Guest Thread registry (`GuestThread`) and deterministic Win32 handle allocator

### Wave 3: Input, Audio & UI (Completed)
- [x] **`RacingControlsView.java` -> `game_core::touch_hud`**:
  - Zero-allocation multi-touch finger tracking (up to 32 simultaneous touch points)
  - Virtual control shapes (pedals, steering d-pad, buttons, analog sticks, action pills)
  - DirectInput scancode generation and key state synchronization
  - Direct quad vertex generator (`HudQuad`, `HudVertex`) for GPU backbuffer rendering
- [x] **`audio_watchdog.cpp` -> `graphics_cache::audio_watchdog`**: Safe atomic watchdog and synthetic DirectSound clock progression.
- [x] **`audio_mixer.h` -> `graphics_cache::audio_engine`**:
  - 128-voice real-time PCM mixing engine in pure Rust
  - Stale streaming ring buffer detection and muting
  - ARM64 NEON vectorized sinc and linear upsampler
  - Safe peak amplitude and clipping statistics tracking

### Wave 4: Graphics & Final Pure Lift (Completed)
- [x] **`vulkan_driver.cpp` -> `graphics_cache::vulkan_driver`**: Safe Vulkan driver opening and proc address resolution.
- [x] **`graphics_cache::native_gles`**: Direct native mobile OpenGL ES 3.0 / 3.2 rendering pipeline, translating D3D9 state directly to GLES and bypassing DXVK translation overhead.
- [x] **`d3d9_bridge.cpp` -> `graphics_cache::d3d9_bridge`**:
  - Direct3D 9 device state tracker and render state management
  - Viewport, texture stages, and primitive drawing counters
  - Resource lifecycle management for textures, surfaces, and vertex/index buffers
- [x] **Pure Native Lift Blueprint**: Native touch HUD and direct Vulkan/GLES pipelines ready for `android-activity` (`NativeActivity`) host switch.

### Wave 5: Direct Performance Governor & Native System Services (Completed)
- [x] **`adpf_manager.cpp` -> `perf_cores::adpf`**:
  - Android Dynamic Performance Framework (ADPF) hint session in pure Rust.
  - Zero-allocation monotonic frame duration calculation and PowerHAL feedback.
- [x] **`audio_watchdog.cpp` -> `graphics_cache::audio_watchdog`**:
  - Full atomic AAudio callback stall detection and synthetic DirectSound clock progression in safe Rust.
- [x] **`performance_tuner.cpp` -> `perf_cores::tuner`**:
  - Direct pure Rust tri-cluster CPU core affinity pinning and thread priority configuration.
- [x] **Complete DXVK Severance & Native Rendering**:
  - Elimination of `ports/dxvk_android` and `ports/dxvk_modules`.
  - Native Direct3D 9 COM implementation in `native_d3d9` and pure Rust `graphics_cache`.
