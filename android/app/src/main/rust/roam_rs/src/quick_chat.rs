//! Pure Rust Quick Chat Protocol Engine & Stick Gesture Classifier.
//! Replaces Java JSON object allocations and toast string building with fast native routines.

use jni::objects::{JClass, JString};
use jni::sys::{jfloat, jint, jstring};
use jni::JNIEnv;

pub const QUICK_CHAT_GROUPS: [[&str; 4]; 4] = [
    ["Hey!", "Nice car!", "Thanks!", "Bye!"],         // Group 0: Up / Hi
    ["Yes", "No", "Sorry!", "lol"],                   // Group 1: Right / Reply
    ["Follow me!", "Wait up!", "Let's go!", "Nice!"], // Group 2: Down / Drive
    ["Race?", "Join my race!", "gg", "One more?"],    // Group 3: Left / Race
];

pub const GROUP_NAMES: [&str; 4] = [
    "Hi (↑)",
    "Reply (→)",
    "Drive (↓)",
    "Race (←)",
];

/// Classifies right analog stick coordinates (X, Y in [-1.0..1.0]) into a radial group:
/// 0 = Up, 1 = Right, 2 = Down, 3 = Left, or -1 if inside deadzone.
pub fn stick_to_radial_group(x: f32, y: f32, deadzone: f32) -> i32 {
    let mag = (x * x + y * y).sqrt();
    if mag < deadzone {
        return -1;
    }

    if y.abs() >= x.abs() {
        if y < 0.0 { 0 } else { 2 }
    } else if x > 0.0 {
        1
    } else {
        3
    }
}

/// Formats received quick chat JSON payload into user-facing notification text.
/// Avoids heavy reflection or external JSON parser dependencies using lightweight scanning.
pub fn format_quick_chat_json(json: &str) -> Option<String> {
    let trimmed = json.trim();
    if trimmed.is_empty() {
        return None;
    }

    // Check for "group" field
    if let Some(group_val) = extract_json_int(trimmed, "group") {
        if group_val >= 0 && (group_val as usize) < GROUP_NAMES.len() {
            let g = group_val as usize;
            let name = GROUP_NAMES[g];
            let phrases = &QUICK_CHAT_GROUPS[g];
            let arrows = ["↑", "→", "↓", "←"];
            let mut s = format!("Chat {}:\n", name);
            for i in 0..4 {
                s.push_str(arrows[i]);
                s.push(' ');
                s.push_str(phrases[i]);
                if i < 3 {
                    s.push_str("  |  ");
                }
            }
            return Some(s);
        }
    }

    // Check for "p" phrase index
    if let Some(p_val) = extract_json_int(trimmed, "p") {
        let p = p_val as usize;
        let group = p / 4;
        let idx = p % 4;

        if group < QUICK_CHAT_GROUPS.len() {
            let phrase = QUICK_CHAT_GROUPS[group][idx];
            let is_me = extract_json_int(trimmed, "me").unwrap_or(0) == 1;
            let name = if is_me {
                "You".to_string()
            } else {
                extract_json_str(trimmed, "name").unwrap_or_else(|| "Racer".to_string())
            };
            return Some(format!("{}: \"{}\"", name, phrase));
        }
    }

    None
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

fn extract_json_str(json: &str, key: &str) -> Option<String> {
    let search = format!("\"{}\"", key);
    if let Some(pos) = json.find(&search) {
        let rest = &json[pos + search.len()..];
        if let Some(colon) = rest.find(':') {
            let after_colon = rest[colon + 1..].trim_start();
            if after_colon.starts_with('"') {
                let inner = &after_colon[1..];
                if let Some(end) = inner.find('"') {
                    return Some(inner[..end].to_string());
                }
            }
        }
    }
    None
}

// ---------------------------------------------------------------------------
// JNI Export Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_QuickChatManager_nativeFormatReceivedChat(
    mut env: JNIEnv,
    _class: JClass,
    json: JString,
) -> jstring {
    if json.is_null() {
        return std::ptr::null_mut();
    }
    let j_str = match env.get_string(&json) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return std::ptr::null_mut(),
    };

    if let Some(formatted) = format_quick_chat_json(&j_str) {
        env.new_string(formatted).map(|js| js.into_raw()).unwrap_or(std::ptr::null_mut())
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_net_QuickChatManager_nativeStickToGroup(
    _env: JNIEnv,
    _class: JClass,
    x: jfloat,
    y: jfloat,
    deadzone: jfloat,
) -> jint {
    stick_to_radial_group(x, y, deadzone)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_stick_gestures() {
        assert_eq!(stick_to_radial_group(0.0, -0.8, 0.4), 0); // Up
        assert_eq!(stick_to_radial_group(0.8, 0.0, 0.4), 1);  // Right
        assert_eq!(stick_to_radial_group(0.0, 0.8, 0.4), 2);  // Down
        assert_eq!(stick_to_radial_group(-0.8, 0.0, 0.4), 3); // Left
        assert_eq!(stick_to_radial_group(0.1, 0.1, 0.4), -1); // Deadzone
    }

    #[test]
    fn test_format_chat_group() {
        let json = r#"{"group": 0}"#;
        let formatted = format_quick_chat_json(json).expect("should format");
        assert!(formatted.contains("Chat Hi (↑):"));
        assert!(formatted.contains("Hey!"));
    }

    #[test]
    fn test_format_chat_phrase() {
        let json = r#"{"p": 2, "name": "Razor", "me": 0}"#;
        let formatted = format_quick_chat_json(json).expect("should format");
        assert_eq!(formatted, "Razor: \"Thanks!\"");

        let json_me = r#"{"p": 0, "name": "Razor", "me": 1}"#;
        let formatted_me = format_quick_chat_json(json_me).expect("should format");
        assert_eq!(formatted_me, "You: \"Hey!\"");
    }
}
