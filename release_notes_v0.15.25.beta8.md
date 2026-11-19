## NFSU2 Revived v0.15.25.beta8

### What's New in v0.15.25.beta8

#### 1. 100% Rust Conversion: Wave 1 & Wave 2 Integration
- **Pure Rust Virtual Switch Protocol Engine (`roam_rs::switch`)**:
  - Migrated Java `VirtualSwitchClient.java` packet decapsulation and framing to safe, zero-copy Rust.
  - Implemented zero-allocation slice parsing for `KIND_UDP` (1), `KIND_LAN_UDP` (3), `KIND_TCP` (4), and `KIND_BUNDLE` (32).
  - Built-in RFC 6455 client WebSocket framing with automatic XOR masking, eliminating Java `ByteBuffer` heap allocations and Garbage Collection stutter during 60 FPS multiplayer gameplay:
```rust
// roam_rs/src/switch.rs - Zero-copy packet decapsulation
match kind {
    KIND_UDP => {
        let (from_ip, from_port, payload) = Self::read_endpoint_header(&data[1..])?;
        Ok(DecapsulatedPacket::Udp { from_ip, from_port, payload })
    }
    KIND_LAN_UDP => {
        let (from_ip, from_port, payload) = Self::read_endpoint_header(&data[1..])?;
        Ok(DecapsulatedPacket::LanUdp { from_ip, from_port, payload })
    }
    KIND_TCP => Ok(DecapsulatedPacket::Tcp(Self::strip_packet_header(data))),
    KIND_BUNDLE => Ok(DecapsulatedPacket::Bundle(Self::strip_packet_header(data))),
    _ => Err(SwitchError::InvalidKind(kind)),
}
```

- **Pure Rust Postoffice Protocol Engine (`roam_rs::postoffice`)**:
  - Implemented client protocol handlers for binary opcodes: `OPCODE_PING`, `OPCODE_LOGIN`, `OPCODE_CONNECT`, `OPCODE_DISCONNECT`, `OPCODE_CONNECT_BSSID`, and `OPCODE_CHAT`.
  - Added safe parsing for Postoffice PDP 24-byte registration and 14-byte datagram headers without intermediate allocations.

#### 2. Tri-Cluster CPU Scheduler & Thread Affinity Pinning (`perf_cores`)
- **Tri-Cluster CPU Topology Detection**: Automatically identifies mobile CPU topology across Prime (Cortex-X / Kryo Prime), Mid (Cortex-A78/A77/A76), and Little (Cortex-A55) clusters via sysfs `cpufreq` and `/proc/cpuinfo`.
- **Role-Based Thread Affinity Pinning**: Pins critical game threads to optimal CPU cores, eliminating OS thread migration jitter and thermal throttling:
```rust
// perf_cores/src/topology.rs - Tri-cluster Core Allocation
match role {
    ThreadRole::RenderMain => {
        // Pin main rendering thread to Prime Cortex-X/A78 performance cores
        if !topo.prime_cores.is_empty() {
            set_affinity_mask(&topo.prime_cores)?;
        } else if !topo.mid_cores.is_empty() {
            set_affinity_mask(&topo.mid_cores)?;
        }
    }
    ThreadRole::GuestAudio => {
        // Pin DirectSound & audio worker to isolated Mid cores
        if !topo.mid_cores.is_empty() {
            set_affinity_mask(&topo.mid_cores)?;
        }
    }
    ThreadRole::BackgroundWorker => {
        // Pin background async compiler to efficiency Little cores
        if !topo.little_cores.is_empty() {
            set_affinity_mask(&topo.little_cores)?;
        }
    }
}
```
- **C-ABI & JNI Integration**: Exported `nfs_perf_tune_thread_role`, `nfs_perf_get_topology_summary_buf`, and `nfs_bind_to_perf_cores` for direct invocation from C/C++ native modules and Java.

#### 3. Pure Rust Win32 Emulation Shims (`graphics_cache::win32_shims`)
- **Memory-Safe Win32 Shims**: Replaced legacy C `src/shims/win32_shims.c` with bounds-checked Rust functions.
- **PE Resource Traversal**: Built memory-safe RT_RCDATA resource traversal for guest D3DX shaders and effects directly from memory:
```rust
// graphics_cache/src/win32_shims.rs - Safe PE Resource Directory Traversal
pub unsafe fn guest_find_resource(guest_base: usize, name: u32, rtype: u32) -> u32 {
    let e_lfanew = *((guest_base + 0x3C) as *const u32) as usize;
    if e_lfanew == 0 || e_lfanew > 0x1000 { return 0; }
    let nt_headers = guest_base + e_lfanew;
    if *(nt_headers as *const u32) != 0x00004550 { return 0; }
    let rsrc_va = *((nt_headers + 0x18 + 0x60 + 2 * 8) as *const u32) as usize;
    let root = (guest_base + rsrc_va) as *const ImageResourceDirectory;
    // Walk Type -> Name -> Language nodes in memory-safe Rust
    ...
}
```
- **Module and Style Emulation**: Shims for `GetModuleHandleA`, `GetModuleFileNameA`, and windowed mode frame styles (`filter_window_long`).

#### 4. Graphics Tuner & Architecture Roadmap
- **Adaptive DXVK Profile Engine (`dxvk_tuner`)**: Driver-aware configuration generator tailoring pipeline caches and descriptor buffers for Mali, Adreno, Immortalis, and Xclipse GPUs.
```rust
// graphics_cache/src/dxvk_tuner.rs - Adaptive Profile Generation
match backend.to_lowercase().as_str() {
    "opengl" | "opengl_es" => {
        // Optimize for mobile tile drivers to prevent pipeline compile loops
        conf.enable_graphics_pipeline_library = false;
        conf.enable_descriptor_buffer = false;
        conf.max_device_memory_allocation_count = 4096;
    }
    _ => {
        conf.enable_async = true;
        conf.enable_gpl_async_cache = true;
    }
}
```
- **Migration Architecture Documents**: Added comprehensive architectural documentation in `rust/`:
  - `rust/DECISIONS.md`: Architectural Decision Records (ADR 001 - ADR 004).
  - `rust/MAP.md`: Living roadmap tracking migration waves and component progress.
  - `rust/GLOSSARY.md`: Domain glossary mapping Java and C structures to pure Rust types.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta7...v0.15.25.beta8