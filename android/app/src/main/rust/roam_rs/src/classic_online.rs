//! Classic EA Nation (eahub.eu) TCP routing and reachability probing in pure Rust.

use std::ffi::CStr;
use std::net::{TcpStream, ToSocketAddrs};
use std::os::raw::{c_char, c_int};
use std::time::Duration;

use jni::objects::{JClass, JString};
use jni::sys::{jboolean, jint};
use jni::JNIEnv;

/// Port constants for classic DirtySock EA services
pub const PORT_REDIRECTOR: u16 = 20920;
pub const PORT_LOBBY: u16 = 20922;
pub const PORT_BUDDY: u16 = 13505;

/// Default socket probe timeout in milliseconds
pub const DEFAULT_PROBE_TIMEOUT_MS: u64 = 4000;

/// Checks if the configured online server is classic EA Nation / eahub.eu.
/// Classic servers use direct TCP sockets and must NOT be routed through WebSocket LocalOnlineBridge.
pub fn is_eahub_classic(server: &str) -> bool {
    let s = server.trim().to_ascii_lowercase();
    if s.is_empty() {
        return false;
    }
    s == "eahub.eu" || s == "adhoc.eahub.eu" || s.ends_with(".eahub.eu")
}

/// Determines whether a given server should be routed through LocalOnlineBridge (WebSocket path).
/// Returns true only if non-empty, not classic eahub, and contains ws/web markers.
pub fn should_use_local_bridge(server: &str) -> bool {
    let s = server.trim();
    if s.is_empty() {
        return false;
    }
    if is_eahub_classic(s) {
        return false;
    }
    // Check if it's explicitly a WebSocket host or custom server
    let lower = s.to_ascii_lowercase();
    lower.starts_with("ws://")
        || lower.starts_with("wss://")
        || lower.contains("nfsu2-web")
        || lower.contains(":8080")
        || lower.contains(":8000")
        || lower.contains(":3000")
}

/// Synchronous TCP reachability probe with timeout.
/// Attempts to resolve host:port and open a TCP connection within timeout_ms.
pub fn probe_port(host: &str, port: u16, timeout_ms: u64) -> bool {
    let host_trim = host.trim();
    if host_trim.is_empty() {
        return false;
    }

    let addr_str = format!("{}:{}", host_trim, port);
    let timeout = Duration::from_millis(timeout_ms);

    if let Ok(mut addrs) = addr_str.to_socket_addrs() {
        for addr in addrs {
            if TcpStream::connect_timeout(&addr, timeout).is_ok() {
                return true;
            }
        }
    }

    false
}

// ---------------------------------------------------------------------------
// C-ABI Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn nfs_classic_is_eahub(server: *const c_char) -> bool {
    if server.is_null() {
        return false;
    }
    if let Ok(s) = CStr::from_ptr(server).to_str() {
        is_eahub_classic(s)
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_classic_should_use_bridge(server: *const c_char) -> bool {
    if server.is_null() {
        return false;
    }
    if let Ok(s) = CStr::from_ptr(server).to_str() {
        should_use_local_bridge(s)
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_classic_probe(host: *const c_char, port: c_int, timeout_ms: c_int) -> bool {
    if host.is_null() || port <= 0 {
        return false;
    }
    let t = if timeout_ms <= 0 { DEFAULT_PROBE_TIMEOUT_MS } else { timeout_ms as u64 };
    if let Ok(h) = CStr::from_ptr(host).to_str() {
        probe_port(h, port as u16, t)
    } else {
        false
    }
}

// ---------------------------------------------------------------------------
// JNI Exports for io.mininxd.nfsu2.net.EahubClassicOnline
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_net_EahubClassicOnline_nativeShouldUseLocalBridge(
    mut env: JNIEnv,
    _class: JClass,
    server: JString,
) -> jboolean {
    if server.is_null() {
        return 0;
    }
    if let Ok(s) = env.get_string(&server) {
        let rust_str = s.to_string_lossy();
        if should_use_local_bridge(&rust_str) { 1 } else { 0 }
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_io_mininxd_nfsu2_net_EahubClassicOnline_nativeProbePort(
    mut env: JNIEnv,
    _class: JClass,
    host: JString,
    port: jint,
    timeout_ms: jint,
) -> jboolean {
    if host.is_null() || port <= 0 {
        return 0;
    }
    let t = if timeout_ms <= 0 { DEFAULT_PROBE_TIMEOUT_MS } else { timeout_ms as u64 };
    if let Ok(h) = env.get_string(&host) {
        let rust_str = h.to_string_lossy();
        if probe_port(&rust_str, port as u16, t) { 1 } else { 0 }
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_eahub_detection() {
        assert!(is_eahub_classic("eahub.eu"));
        assert!(is_eahub_classic("adhoc.eahub.eu"));
        assert!(is_eahub_classic("test.eahub.eu"));
        assert!(!is_eahub_classic("custom-server.com"));
        assert!(!is_eahub_classic(""));
    }

    #[test]
    fn test_should_use_bridge() {
        assert!(!should_use_local_bridge("eahub.eu"));
        assert!(!should_use_local_bridge("adhoc.eahub.eu"));
        assert!(should_use_local_bridge("ws://192.168.1.100:8080"));
        assert!(should_use_local_bridge("nfsu2-web.online"));
    }
}
