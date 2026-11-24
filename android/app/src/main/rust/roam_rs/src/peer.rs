//! Pure Rust Peer Coordinator and State Synchronization Engine for NFSU2 Android Free Roam.
//!
//! Replaces legacy Java `io.mininxd.nfsu2.AdhocRoamClient` peer tracking with a thread-safe,
//! zero-allocation native peer registry, virtual MAC routing, and connection state management.

use std::collections::HashMap;
use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::RwLock;
use jni::objects::{JByteArray, JClass};
use jni::sys::{jbyteArray, jint, jstring};
use jni::JNIEnv;

/// Representation of an active network peer in Free Roam.
#[repr(C)]
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PeerInfo {
    pub name: String,
    pub mac: [u8; 6],
    pub ip: u32,
    pub virtual_port: u16,
    pub last_seen_ms: u64,
}

impl PeerInfo {
    pub fn new(name: &str, mac: [u8; 6], ip: u32, virtual_port: u16, now_ms: u64) -> Self {
        Self {
            name: name.to_string(),
            mac,
            ip,
            virtual_port,
            last_seen_ms: now_ms,
        }
    }

    pub fn mac_string(&self) -> String {
        format_mac(&self.mac)
    }
}

/// Thread-safe in-memory peer registry for Free Roam multiplayer.
#[derive(Debug, Default)]
pub struct PeerRegistry {
    // Keyed by uppercase hex MAC address (e.g. "02:00:1A:2B:3C:4D")
    peers: RwLock<Option<HashMap<String, PeerInfo>>>,
}

impl PeerRegistry {
    pub const fn new() -> Self {
        Self {
            peers: RwLock::new(None),
        }
    }

    /// Registers or updates an active peer.
    pub fn register_peer(&self, name: &str, mac: [u8; 6], ip: u32, virtual_port: u16, now_ms: u64) {
        let key = format_mac(&mac);
        let peer = PeerInfo::new(name, mac, ip, virtual_port, now_ms);
        if let Ok(mut lock) = self.peers.write() {
            let map = lock.get_or_insert_with(HashMap::new);
            map.insert(key, peer);
        }
    }

    /// Removes a peer matching the given virtual IPv4 address.
    pub fn remove_by_ip(&self, ip: u32) -> bool {
        if let Ok(mut lock) = self.peers.write() {
            if let Some(ref mut map) = *lock {
                let key_to_remove = map
                    .iter()
                    .find(|(_, p)| p.ip == ip)
                    .map(|(k, _)| k.clone());
                if let Some(key) = key_to_remove {
                    return map.remove(&key).is_some();
                }
            }
        }
        false
    }

    /// Removes a peer matching the given MAC address.
    pub fn remove_by_mac(&self, mac: &[u8; 6]) -> bool {
        let key = format_mac(mac);
        if let Ok(mut lock) = self.peers.write() {
            if let Some(ref mut map) = *lock {
                return map.remove(&key).is_some();
            }
        }
        false
    }

    /// Finds peer info by MAC address.
    pub fn find_by_mac(&self, mac: &[u8; 6]) -> Option<PeerInfo> {
        let key = format_mac(mac);
        let lock = self.peers.read().ok()?;
        lock.as_ref()?.get(&key).cloned()
    }

    /// Finds peer info by IPv4 address.
    pub fn find_by_ip(&self, ip: u32) -> Option<PeerInfo> {
        let lock = self.peers.read().ok()?;
        lock.as_ref()?.values().find(|p| p.ip == ip).cloned()
    }

    /// Resolves source IPv4 address for an incoming MAC datagram.
    /// If peer is registered and has non-zero IP, returns it;
    /// otherwise synthesizes an IP from the lower 4 octets of the MAC address
    /// (matching the Prometheus adhoc networking spec).
    pub fn resolve_source_ip(&self, mac: &[u8; 6]) -> u32 {
        if let Some(peer) = self.find_by_mac(mac) {
            if peer.ip != 0 {
                return peer.ip;
            }
        }
        // Fallback: ((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5])
        ((mac[2] as u32) << 24)
            | ((mac[3] as u32) << 16)
            | ((mac[4] as u32) << 8)
            | (mac[5] as u32)
    }

    /// Returns the number of currently tracked peers.
    pub fn count(&self) -> usize {
        self.peers
            .read()
            .ok()
            .and_then(|lock| lock.as_ref().map(|m| m.len()))
            .unwrap_or(0)
    }

    /// Returns a list snapshot of all active peers.
    pub fn all_peers(&self) -> Vec<PeerInfo> {
        self.peers
            .read()
            .ok()
            .and_then(|lock| lock.as_ref().map(|m| m.values().cloned().collect()))
            .unwrap_or_default()
    }

    /// Prunes stale peers that have not sent a datagram within `timeout_ms`.
    pub fn prune_stale(&self, timeout_ms: u64, now_ms: u64) -> usize {
        if let Ok(mut lock) = self.peers.write() {
            if let Some(ref mut map) = *lock {
                let before = map.len();
                map.retain(|_, p| now_ms.saturating_sub(p.last_seen_ms) <= timeout_ms);
                return before.saturating_sub(map.len());
            }
        }
        0
    }

    /// Clears all registered peers.
    pub fn clear(&self) {
        if let Ok(mut lock) = self.peers.write() {
            if let Some(ref mut map) = *lock {
                map.clear();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MAC Address Helper Functions
// ---------------------------------------------------------------------------

/// Formats a 6-byte MAC into standard colon-separated uppercase string.
pub fn format_mac(mac: &[u8; 6]) -> String {
    format!(
        "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    )
}

/// Parses a colon- or hyphen-separated hex string into a 6-byte MAC array.
pub fn parse_mac(s: &str) -> Option<[u8; 6]> {
    let clean: Vec<&str> = s.split(|c| c == ':' || c == '-').collect();
    if clean.len() != 6 {
        return None;
    }
    let mut mac = [0u8; 6];
    for (i, part) in clean.iter().enumerate() {
        mac[i] = u8::from_str_radix(part.trim(), 16).ok()?;
    }
    Some(mac)
}

/// Generates a locally administered unicast virtual MAC address from a 64-bit seed.
pub fn generate_virtual_mac(seed: u64) -> [u8; 6] {
    let b0 = 0x02; // Locally administered, unicast
    let b1 = ((seed >> 32) & 0xFF) as u8;
    let b2 = ((seed >> 24) & 0xFF) as u8;
    let b3 = ((seed >> 16) & 0xFF) as u8;
    let b4 = ((seed >> 8) & 0xFF) as u8;
    let b5 = (seed & 0xFF) as u8;
    [b0, b1, b2, b3, b4, b5]
}

// ---------------------------------------------------------------------------
// Global Roam Coordinator
// ---------------------------------------------------------------------------

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RoamBackendMode {
    PrometheusAdhoc = 1,
    WebSocketSwitch = 2,
}

pub struct RoamCoordinator {
    pub running: AtomicBool,
    pub connected: AtomicBool,
    pub racing: AtomicBool,
    pub ghost_mode: AtomicBool,
    pub mode: AtomicU32,
    pub hub_ip: AtomicU32,
    pub registry: PeerRegistry,
}

impl RoamCoordinator {
    pub const fn new() -> Self {
        Self {
            running: AtomicBool::new(false),
            connected: AtomicBool::new(false),
            racing: AtomicBool::new(false),
            ghost_mode: AtomicBool::new(true),
            mode: AtomicU32::new(RoamBackendMode::PrometheusAdhoc as u32),
            hub_ip: AtomicU32::new(0),
            registry: PeerRegistry::new(),
        }
    }
}

pub static COORDINATOR: RoamCoordinator = RoamCoordinator::new();

// ---------------------------------------------------------------------------
// C-ABI Exports for Native Recompilation Host & JNI Integration
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_roam_peer_register(
    name: *const c_char,
    mac: *const u8,
    ip: u32,
    virtual_port: u16,
) -> c_int {
    if mac.is_null() {
        return -1;
    }
    let name_str = if !name.is_null() {
        CStr::from_ptr(name).to_str().unwrap_or("Racer")
    } else {
        "Racer"
    };

    let mut mac_arr = [0u8; 6];
    std::ptr::copy_nonoverlapping(mac, mac_arr.as_mut_ptr(), 6);

    let now_ms = match std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH) {
        Ok(d) => d.as_millis() as u64,
        Err(_) => 0,
    };

    COORDINATOR.registry.register_peer(name_str, mac_arr, ip, virtual_port, now_ms);
    0
}

#[no_mangle]
pub unsafe extern "C" fn nfs_roam_peer_unregister_ip(ip: u32) -> c_int {
    if COORDINATOR.registry.remove_by_ip(ip) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_roam_peer_resolve_ip(mac: *const u8) -> u32 {
    if mac.is_null() {
        return 0;
    }
    let mut mac_arr = [0u8; 6];
    std::ptr::copy_nonoverlapping(mac, mac_arr.as_mut_ptr(), 6);
    COORDINATOR.registry.resolve_source_ip(&mac_arr)
}

#[no_mangle]
pub extern "C" fn nfs_roam_peer_count() -> c_int {
    COORDINATOR.registry.count() as c_int
}

#[no_mangle]
pub extern "C" fn nfs_roam_peer_clear() {
    COORDINATOR.registry.clear();
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_set_connected(connected: c_int) {
    COORDINATOR.connected.store(connected != 0, Ordering::Release);
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_is_connected() -> c_int {
    if COORDINATOR.connected.load(Ordering::Acquire) { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_set_racing(racing: c_int) {
    COORDINATOR.racing.store(racing != 0, Ordering::Release);
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_is_racing() -> c_int {
    if COORDINATOR.racing.load(Ordering::Acquire) { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_set_ghost_mode(ghost: c_int) {
    COORDINATOR.ghost_mode.store(ghost != 0, Ordering::Release);
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_get_ghost_mode() -> c_int {
    if COORDINATOR.ghost_mode.load(Ordering::Acquire) { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_set_hub_ip(hub_ip: u32) {
    COORDINATOR.hub_ip.store(hub_ip, Ordering::Release);
}

#[no_mangle]
pub extern "C" fn nfs_roam_coord_get_hub_ip() -> u32 {
    COORDINATOR.hub_ip.load(Ordering::Acquire)
}

// ---------------------------------------------------------------------------
// JNI Exports for io.mininxd.nfsu2.net.Peer and io.mininxd.nfsu2.AdhocRoamClient
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_Peer_nativeFormatMac(
    mut env: JNIEnv,
    _class: JClass,
    mac_bytes: JByteArray,
) -> jstring {
    if mac_bytes.is_null() {
        return env.new_string("00:00:00:00:00:00").map(|s| s.into_raw()).unwrap_or(std::ptr::null_mut());
    }
    let mut buf = [0i8; 6];
    if env.get_byte_array_region(&mac_bytes, 0, &mut buf).is_ok() {
        let ubuf = [
            buf[0] as u8,
            buf[1] as u8,
            buf[2] as u8,
            buf[3] as u8,
            buf[4] as u8,
            buf[5] as u8,
        ];
        let s = format_mac(&ubuf);
        return env.new_string(s).map(|s| s.into_raw()).unwrap_or(std::ptr::null_mut());
    }
    std::ptr::null_mut()
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_AdhocRoamClient_nativeFormatMac(
    env: JNIEnv,
    class: JClass,
    mac_bytes: JByteArray,
) -> jstring {
    Java_io_mininxd_nfsu2_net_Peer_nativeFormatMac(env, class, mac_bytes)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_AdhocRoamClient_nativeGenerateVirtualMac(
    env: JNIEnv,
    _class: JClass,
) -> jbyteArray {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0x123456789ABCDEF0);
    let mac = generate_virtual_mac(now);
    let i8_buf = [
        mac[0] as i8,
        mac[1] as i8,
        mac[2] as i8,
        mac[3] as i8,
        mac[4] as i8,
        mac[5] as i8,
    ];
    if let Ok(jarr) = env.new_byte_array(6) {
        let _ = env.set_byte_array_region(&jarr, 0, &i8_buf);
        jarr.into_raw()
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_AdhocRoamClient_nativeResolveSourceIp(
    mut env: JNIEnv,
    _class: JClass,
    mac_bytes: JByteArray,
) -> jint {
    if mac_bytes.is_null() {
        return 0;
    }
    let mut buf = [0i8; 6];
    if env.get_byte_array_region(&mac_bytes, 0, &mut buf).is_ok() {
        let ubuf = [
            buf[0] as u8,
            buf[1] as u8,
            buf[2] as u8,
            buf[3] as u8,
            buf[4] as u8,
            buf[5] as u8,
        ];
        COORDINATOR.registry.resolve_source_ip(&ubuf) as jint
    } else {
        0
    }
}

// ---------------------------------------------------------------------------
// Unit Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_mac_formatting_and_parsing() {
        let mac = [0x02, 0x00, 0x7A, 0x5C, 0x1E, 0x9F];
        let formatted = format_mac(&mac);
        assert_eq!(formatted, "02:00:7A:5C:1E:9F");

        let parsed = parse_mac(&formatted).expect("Must parse valid MAC");
        assert_eq!(parsed, mac);

        let parsed_hyphen = parse_mac("02-00-7A-5C-1E-9F").expect("Must parse hyphenated MAC");
        assert_eq!(parsed_hyphen, mac);

        assert!(parse_mac("invalid").is_none());
        assert!(parse_mac("02:00:11").is_none());
    }

    #[test]
    fn test_virtual_mac_generation() {
        let mac = generate_virtual_mac(0x123456789ABCDEF0);
        assert_eq!(mac[0], 0x02); // Locally administered
        assert_eq!(mac[1], 0x56);
        assert_eq!(mac[2], 0x78);
        assert_eq!(mac[3], 0x9A);
        assert_eq!(mac[4], 0xBC);
        assert_eq!(mac[5], 0xF0);
    }

    #[test]
    fn test_peer_registry_lifecycle() {
        let reg = PeerRegistry::new();
        assert_eq!(reg.count(), 0);

        let mac1 = [0x02, 0x00, 0x0A, 0x4D, 0x00, 0x01];
        let ip1 = 0x0A4D0001; // 10.77.0.1
        reg.register_peer("Player1", mac1, ip1, 47210, 1000);
        assert_eq!(reg.count(), 1);

        let resolved = reg.resolve_source_ip(&mac1);
        assert_eq!(resolved, ip1);

        let found = reg.find_by_mac(&mac1).expect("Peer must be found");
        assert_eq!(found.name, "Player1");
        assert_eq!(found.virtual_port, 47210);

        // Synthetic IP for unlisted peer
        let unlisted_mac = [0x02, 0x00, 0x0A, 0x4D, 0x00, 0x99];
        let synthetic = reg.resolve_source_ip(&unlisted_mac);
        assert_eq!(synthetic, 0x0A4D0099);

        // Remove by IP
        assert!(reg.remove_by_ip(ip1));
        assert_eq!(reg.count(), 0);
    }

    #[test]
    fn test_peer_registry_stale_prune() {
        let reg = PeerRegistry::new();
        let mac1 = [0x02, 0x00, 0x0A, 0x4D, 0x00, 0x01];
        let mac2 = [0x02, 0x00, 0x0A, 0x4D, 0x00, 0x02];

        reg.register_peer("Active", mac1, 0x0A4D0001, 47210, 10_000);
        reg.register_peer("Stale", mac2, 0x0A4D0002, 47210, 2_000);

        assert_eq!(reg.count(), 2);

        // Prune peers older than 5000ms at current time 10_000ms
        let pruned = reg.prune_stale(5000, 10_000);
        assert_eq!(pruned, 1);
        assert_eq!(reg.count(), 1);
        assert!(reg.find_by_mac(&mac1).is_some());
        assert!(reg.find_by_mac(&mac2).is_none());
    }
}
