# Glossary - NFSU2 Android to Rust Migration

| Original Term (Java/C) | Category | Meaning | Rust Type / Module | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `VirtualSwitchClient` | Java Network | RFC 6455 WebSocket client connecting to `/lan` switch | `roam_rs::switch::VirtualSwitchClient` | Replaces Java ByteBuffer & socket loops |
| `KIND_UDP` (1) | Protocol | Datagram frame for Free Roam multiplayer updates | `roam_rs::switch::PacketKind::Udp` | 16-byte header with src/dst VIP & ports |
| `KIND_LAN_UDP` (3) | Protocol | LAN mode broadcast/datagram frame | `roam_rs::switch::PacketKind::LanUdp` | 24-byte header |
| `KIND_TCP` (4) | Protocol | Virtual LAN host TCP stream | `roam_rs::switch::PacketKind::Tcp` | 24-byte header with op/cid/payload |
| `KIND_BUNDLE` (32) | Protocol | Batched multi-frame container | `roam_rs::switch::BundleIterator` | Zero-copy subframe iterator |
| `nfs_log*` | C Platform | Categorized host & recompilation logging | `nfs_platform::log` | C ABI `nfs_log_init`, `nfs_log`, etc. |
| `nfs_cat_t` | C Enum | Logging categories (BOOT, CPU, WIN32, D3D9...) | `nfs_platform::LogCategory` | Rust enum with C representation (`#[repr(C)]`) |
| `roam_affinity.c` | C System | Android CPU big.LITTLE performance core pinning | `perf_cores::CpuCluster` | Exported as `nfs_bind_to_perf_cores` |
| `GraphicsCacheManager` | Java Storage | Precompiled shader & pipeline disk cache manager | `graphics_cache::pipeline_cache` | Zero-copy memory mapped shader storage |
| `RacingControlsView` | Java UI | On-screen steering slider, touch buttons | `game_core::touch_hud` | Rust touch event state & vertex overlay |
| `AdhocRoamClient` | Java / C Bridge| Free Roam peer manager and room coordination | `roam_rs::engine` & `roam_rs::sync_guard` | Replaces Java synchronized maps with lockless atomics |
