//! Pure Rust Server API Client Engine & FNV-1a Room Hasher for NFSU2 Android.
//! Replaces Java JSON parsing and string allocations for `/players` and `/vault` REST communications.

use jni::objects::{JClass, JIntArray, JString};
use jni::sys::{jboolean, jint, jstring};
use jni::JNIEnv;

/// Computes FNV-1a room hash matching nfsu2-web and roam_core.c rc_room_hash.
/// Empty string returns 0 (public open world).
pub fn room_hash(name: &str) -> i32 {
    let s = name.trim();
    if s.is_empty() {
        return 0;
    }

    let mut h: u32 = 0x811c9dc5;
    let mut kept = 0;

    for &b in s.as_bytes() {
        if b == 0x20 || (0x09..=0x0D).contains(&b) {
            continue;
        }
        let mut ub = b;
        if (b'A'..=b'Z').contains(&ub) {
            ub += 0x20;
        }
        h = (h ^ (ub as u32)).wrapping_mul(0x01000193);
        kept += 1;
    }

    if kept == 0 {
        return 0;
    }
    if h == 0 {
        1
    } else {
        h as i32
    }
}

/// Normalizes server host input string into an HTTP/HTTPS base URL.
pub fn resolve_http_base_url(host_input: &str, default_host: &str) -> String {
    let raw = host_input.trim();
    let effective = if raw.is_empty() { default_host } else { raw };

    let mut host = effective;
    let mut use_ssl = true;
    let mut port = 443;

    if host.starts_with("wss://") {
        host = &host[6..];
        use_ssl = true;
    } else if host.starts_with("ws://") {
        host = &host[5..];
        use_ssl = false;
        port = 80;
    } else if host.starts_with("https://") {
        host = &host[8..];
        use_ssl = true;
    } else if host.starts_with("http://") {
        host = &host[7..];
        use_ssl = false;
        port = 80;
    }

    if let Some(slash_idx) = host.find('/') {
        host = &host[..slash_idx];
    }

    if let Some(colon_idx) = host.find(':') {
        let port_part = &host[colon_idx + 1..];
        if let Ok(p) = port_part.parse::<i32>() {
            port = p;
        }
        host = &host[..colon_idx];
    }

    let is_default_port = (use_ssl && port == 443) || (!use_ssl && port == 80);
    if is_default_port {
        format!("{}://{}", if use_ssl { "https" } else { "http" }, host)
    } else {
        format!("{}://{}:{}", if use_ssl { "https" } else { "http" }, host, port)
    }
}

/// Classifies whether a host targets the modern WebSocket bridge vs legacy EA Nation / adhoc.
pub fn is_ws_host(host: &str) -> bool {
    crate::classic_online::should_use_local_bridge(host)
}

/// Parses JSON response from `/players` endpoint into `[players, roam, online, room]`.
pub fn parse_player_stats(json_str: &str, _room_hash: i32) -> Option<[i32; 4]> {
    let trimmed = json_str.trim();
    if trimmed.is_empty() {
        return None;
    }

    let players = extract_json_int(trimmed, "players").unwrap_or(-1);
    let roam = extract_json_int(trimmed, "roam").unwrap_or(-1);
    let online = extract_json_int(trimmed, "online").unwrap_or(-1);
    let room = extract_json_int(trimmed, "room").unwrap_or(roam);

    Some([players, roam, online, room])
}

fn extract_json_int(json: &str, key: &str) -> Option<i32> {
    let search = format!("\"{}\"", key);
    if let Some(pos) = json.find(&search) {
        let rest = &json[pos + search.len()..];
        if let Some(colon) = rest.find(':') {
            let after_colon = rest[colon + 1..].trim_start();
            let mut num_str = String::new();
            for c in after_colon.chars() {
                if c.is_ascii_digit() || (c == '-' && num_str.is_empty()) {
                    num_str.push(c);
                } else if !num_str.is_empty() {
                    break;
                }
            }
            return num_str.parse::<i32>().ok();
        }
    }
    None
}

// ---------------------------------------------------------------------------
// JNI Export Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_ServerApiClient_nativeRoomHash(
    mut env: JNIEnv,
    _class: JClass,
    name: JString,
) -> jint {
    if name.is_null() {
        return 0;
    }
    let n_str = match env.get_string(&name) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    room_hash(&n_str)
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_ServerApiClient_nativeGetHttpBaseUrl(
    mut env: JNIEnv,
    _class: JClass,
    host: JString,
    default_host: JString,
) -> jstring {
    let h_str = if !host.is_null() {
        env.get_string(&host).map(|s| s.to_string_lossy().to_string()).unwrap_or_default()
    } else {
        String::new()
    };
    let def_str = if !default_host.is_null() {
        env.get_string(&default_host).map(|s| s.to_string_lossy().to_string()).unwrap_or_default()
    } else {
        "roam.nfsu2.online".to_string()
    };

    let url = resolve_http_base_url(&h_str, &def_str);
    env.new_string(url).map(|js| js.into_raw()).unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_ServerApiClient_nativeIsWsHost(
    mut env: JNIEnv,
    _class: JClass,
    host: JString,
) -> jboolean {
    if host.is_null() {
        return 0;
    }
    let h_str = match env.get_string(&host) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    if is_ws_host(&h_str) { 1 } else { 0 }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_ServerApiClient_nativeParsePlayersJson(
    mut env: JNIEnv,
    _class: JClass,
    json: JString,
    room_hash: jint,
    out_stats: JIntArray,
) -> jint {
    if json.is_null() || out_stats.is_null() {
        return 0;
    }
    let j_str = match env.get_string(&json) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };

    if let Some(stats) = parse_player_stats(&j_str, room_hash) {
        let _ = env.set_int_array_region(&out_stats, 0, &stats);
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_room_hash() {
        assert_eq!(room_hash(""), 0);
        assert_eq!(room_hash("   "), 0);
        assert_ne!(room_hash("bayview"), 0);
        assert_eq!(room_hash("BAYVIEW"), room_hash("bayview"));
    }

    #[test]
    fn test_resolve_url() {
        assert_eq!(resolve_http_base_url("wss://roam.example.com", "default"), "https://roam.example.com");
        assert_eq!(resolve_http_base_url("ws://roam.example.com:8080", "default"), "http://roam.example.com:8080");
        assert_eq!(resolve_http_base_url("", "roam.default.com"), "https://roam.default.com");
    }

    #[test]
    fn test_parse_stats() {
        let json = r#"{"players": 42, "roam": 10, "online": 32, "room": 4}"#;
        let stats = parse_player_stats(json, 0).expect("should parse");
        assert_eq!(stats, [42, 10, 32, 4]);
    }
}
