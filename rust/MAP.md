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

### Wave 6: Online Network Routing, Settings Engine & Zero-Overhead Ring Logging (Completed)
- [x] **`EahubClassicOnline.java` -> `roam_rs::classic_online`**:
  - Direct TCP reachability socket probe with timeout in pure Rust (`probe_port`).
  - Classic EA Nation vs WebSocket bridge routing classifier (`should_use_local_bridge`, `is_eahub_classic`).
  - Safe JNI bindings (`nativeShouldUseLocalBridge`, `nativeProbePort`).
- [x] **`GameOptions.java` -> `game_core::settings` & `graphics_cache::settings_engine`**:
  - In-place configuration parsing and file mutation for `NFSUnderground2.WidescreenFix.ini`, `NFSUnderground2.WidescreenFix.dat`, and `NFSU2ExtraOptionsSettings.ini`.
  - Native EU/NA region asset detection via `GLOBALTEXTURESROW.BIN` probe.
  - Safe JNI bindings (`nativeSyncWidescreenIni`, `nativeSyncWidescreenDat`, `nativeSyncExtraOptionsIni`, `nativeDetectRegion`).
- [x] **`GameLog.java` & `NetLog.java` -> `perf_cores::logger`**:
  - 1024-entry zero-allocation lock-free in-memory circular ring buffer in safe Rust.
  - Microsecond-precision epoch timestamp generation and categorized entry tracking.
  - Eliminates JVM garbage collection pauses during high-frequency networking and racing inputs.
  - JNI bindings (`nativeRingLog`, `nativeRingGetBytes`).

### Wave 7: Driver Archive Security, Semantic Version Engine & Pure Rust LAN TCP Bridge (Completed)
- [x] **`ReleaseVersion.java` & `ReleaseUpdates.java` -> `game_core::version`**:
  - Zero-allocation semantic version parser and comparator (`parse_version`, `same_version`, `newer_version`, `is_update_available`).
  - Supports numeric dot-hierarchy and pre-release levels (alpha, beta, pre, rc).
  - Single-pass Markdown release notes stripper to clean plain text (`strip_markdown`).
  - Safe JNI bindings (`nativeSameVersion`, `nativeNewer`, `nativeIsUpdateAvailable`, `nativeStripMarkdown`).
- [x] **`DriverArchive.java` -> `graphics_cache::driver_inspector`**:
  - Pure Rust ARM64 ELF shared library header validator (`validate_arm64_elf_file`, `validate_arm64_elf_header`).
  - Fast recursive directory deletion and unlinking (`nativeDeleteRecursive`).
  - Safe JNI bindings (`nativeValidateLibrary`, `nativeDeleteRecursive`).
- [x] **`LocalOnlineBridge.java` & `VirtualSwitchClient.java` -> `roam_rs::local_bridge`**:
  - Zero-allocation 24-byte LAN TCP frame encoder and decoder (`encode_tcp_frame`, `parse_tcp_frame`).
  - In-place ASCII IP rewriter (`rewrite_server_addresses` from `10.77.0.1` to `127.0.0.1`).
  - RFC/protocol constants: `TCP_OP_SYN`, `TCP_OP_SYNACK`, `TCP_OP_RST`, `TCP_OP_DATA`, `TCP_OP_FIN`, `VHOST_VIP`.
  - Safe JNI bindings (`nativeBuildTcpFrame`, `nativeRewriteServerAddresses`, `nativeParseTcpHeader`).

### Wave 8: GPU Driver Discovery, Quick Chat Protocol, Server API & Pipeline Cache Management (Completed)
- [x] **`GpuDrivers.java` -> `graphics_cache::driver_inspector`**:
  - Pure Rust GPU driver package scanner and `driver.json` / `meta.json` metadata parser (`scan_gpu_drivers`).
  - Strict path traversal validation, ELF ARM64 `.so` existence check.
  - Safe JNI bindings (`nativeScanDrivers`).
- [x] **`GraphicsCache.java` -> `graphics_cache::pipeline_cache`**:
  - Pure Rust atomic pipeline cache backup and rotation engine (`rotate_and_reset_cache_dir`).
  - Timestamped directory backup (`dxvk-v2-reset-<timestamp>`).
  - Safe JNI bindings (`nativeResetCache`).
- [x] **`QuickChatManager.java` -> `roam_rs::quick_chat`**:
  - Pure Rust radial stick gesture classifier (`stick_to_radial_group`) with deadzone thresholding.
  - User notification JSON formatter (`format_quick_chat_json`) replacing Java JSONObject parsing.
  - Safe JNI bindings (`nativeFormatReceivedChat`, `nativeStickToGroup`).
- [x] **`ServerApiClient.java` -> `roam_rs::server_api`**:
  - FNV-1a room hash matching nfsu2-web and roam companion in pure Rust (`room_hash`).
  - Server URL resolver and WebSocket host classifier (`resolve_http_base_url`, `is_ws_host`).
  - Lightweight JSON player statistics parser for `/players` endpoint (`parse_player_stats`).
  - Safe JNI bindings (`nativeRoomHash`, `nativeGetHttpBaseUrl`, `nativeIsWsHost`, `nativeParsePlayersJson`).

### Wave 9: Virtual Switch Framing, Postoffice PDP Engine & Native Crash Diagnostics (Completed)
- [x] **`VirtualSwitchClient.java` -> `roam_rs::switch`**:
  - Zero-allocation RFC 6455 WebSocket client framing and masking engine (`build_ws_frame`).
  - Fast KIND_UDP (1) datagram encoder (`build_udp_frame`) and parser (`parse_udp_frame`).
  - Eliminates Java `ByteBuffer.allocate()` and `ByteArrayOutputStream` GC spikes during multiplayer.
  - Safe JNI bindings (`nativeBuildWsFrame`, `nativeBuildUdpFrame`, `nativeParseUdpFrame`).
- [x] **`AdhocPostofficeClient.java` -> `roam_rs::postoffice`**:
  - Prometheus Adhoc protocol packet builders: LOGIN, CONNECT_BSSID, and INIT_PDP.
  - Zero-copy 14-byte Postoffice datagram header decoder (`parse_postoffice_dgram_header`).
  - Safe JNI bindings (`nativeBuildLogin`, `nativeBuildConnectBssid`, `nativeBuildInitPdp`, `nativeParseDgramHeader`).
- [x] **`Diagnostics.java` -> `perf_cores::diagnostics`**:
  - Fast zero-allocation logcat threadtime line compactor (`compact_threadtime_line`).
  - Zero-regex diagnostic log filter (`is_key_line`, `is_noise_line`, `filter_diagnostics_log`).
  - Safe JNI bindings (`nativeFilterLogSummary`).

### Wave 10: Platform GameManager AGDK, Zero-GC Telemetry HUD & Native Peer Routing (Completed)
- [x] **`AndroidGameManagerBridge.java` -> `perf_cores::game_manager`**:
  - Pure Rust Android GameManager & GameState AGDK platform integration (`set_game_state_uninterruptible`, `set_game_state_loading`).
  - Prevents compositor down-throttling to 30Hz and forces 60+ FPS scheduling without Java reflection overhead.
  - Keeps display awake (`FLAG_KEEP_SCREEN_ON`) via native Window flags.
  - Safe JNI bindings (`nativeSetGameplayMode`, `nativeSetLoadingMode`).
- [x] **`DiagnosticOverlay.java` -> `perf_cores::diagnostics`**:
  - Pure Rust telemetry overlay formatter (`format_overlay_text`) replacing heavy Java `StringBuilder` and `String.format` GC spikes on the UI thread every 1000ms.
  - Supports both full telemetry inspector and compact in-game racing HUD.
  - Safe JNI bindings (`nativeFormatOverlayText`).
- [x] **`net/Peer.java` & `AdhocRoamClient.java` -> `roam_rs::peer`**:
  - Pure Rust MAC string formatting (`format_mac`) eliminating repetitive Java heap allocations.
  - Safe virtual MAC generation (`generate_virtual_mac`) with locally administered unicast bit.
  - Native Prometheus Adhoc synthetic IP routing (`resolve_source_ip`).
  - Safe JNI bindings (`nativeFormatMac`, `nativeGenerateVirtualMac`, `nativeResolveSourceIp`).
