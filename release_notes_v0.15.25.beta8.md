## NFSU2 Revived v0.15.25.beta8

### What's New in v0.15.25.beta8

#### 1. 100% Rust Conversion: Wave 1 & Wave 2 Integration
- **Pure Rust Virtual Switch Protocol Engine (`roam_rs::switch`)**:
  - Migrated Java `VirtualSwitchClient.java` packet decapsulation and framing to safe, zero-copy Rust.
  - Implemented zero-allocation slice parsing for `KIND_UDP` (1), `KIND_LAN_UDP` (3), `KIND_TCP` (4), and `KIND_BUNDLE` (32).
  - Built-in RFC 6455 client WebSocket framing with automatic XOR masking, eliminating Java `ByteBuffer` heap allocations and Garbage Collection stutter during 60 FPS multiplayer gameplay.
- **Pure Rust Win32 Emulation Shims (`graphics_cache::win32_shims`)**:
  - Ported C `src/shims/win32_shims.c` into memory-safe Rust with C-ABI compatibility.
  - Added PE resource directory parsing (`guest_find_resource`, `guest_load_resource`, `guest_sizeof_resource`) for guest RT_RCDATA effect files (D3DX shaders).
  - Shims for `GetModuleHandleA`, `GetModuleFileNameA`, and windowed mode frame styles (`SetWindowLongA`).
- **Pure Rust Platform Logging (`rust/crates/nfs_platform`)**:
  - Replaced legacy C `src/platform/nfs_log.c` with a thread-safe Rust logging engine.
  - C-ABI exports `nfs_log_init`, `nfs_log_stamp`, `nfs_log_dir`, and `nfs_log_flush`.
- **CPU Performance Core Affinity Pinning in Rust**:
  - Ported `roam_affinity.c` to Rust, exporting `nfs_bind_to_perf_cores` directly from `perf_cores`.

#### 2. Architecture & Roadmap Documentation
- Added whole-app migration architecture documents:
  - `rust/DECISIONS.md`: Architectural Decision Records (ADRs 001-004).
  - `rust/MAP.md`: Roadmap tracking migration waves and component progress.
  - `rust/GLOSSARY.md`: Domain glossary mapping Java and C structures to Rust types.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta7...v0.15.25.beta8
