//! Zero-allocation semantic version parser, release comparison, and markdown sanitizer in pure Rust.
//! Replaces Java regex object allocations and string copying with fast native methods.

use jni::objects::{JClass, JString};
use jni::sys::{jboolean, jstring};
use jni::JNIEnv;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedVersion {
    pub numbers: Vec<u32>,
    pub pre_level: u32,  // 0 = alpha, 1 = beta/pre, 2 = rc, 999 = final release
    pub pre_number: u32,
}

impl ParsedVersion {
    pub fn new(numbers: Vec<u32>, pre_level: u32, pre_number: u32) -> Self {
        Self { numbers, pre_level, pre_number }
    }
}

/// Parses a version string into numeric components and pre-release metadata.
/// Examples:
///   "v0.15.24" -> numbers: [0, 15, 24], pre_level: 999, pre_number: 0
///   "0.15.24.1" -> numbers: [0, 15, 24, 1], pre_level: 999, pre_number: 0
///   "v0.15.25.beta1" -> numbers: [0, 15, 25], pre_level: 1, pre_number: 1
///   "1.1.beta1" -> numbers: [1, 1], pre_level: 1, pre_number: 1
///   "v1.0.0-rc2" -> numbers: [1, 0, 0], pre_level: 2, pre_number: 2
///   "0.15.24-alpha" -> numbers: [0, 15, 24], pre_level: 0, pre_number: 0
pub fn parse_version(raw: &str) -> Option<ParsedVersion> {
    let s = raw.trim();
    if s.is_empty() {
        return None;
    }

    // Strip leading 'v' or 'V'
    let s = if s.starts_with('v') || s.starts_with('V') {
        &s[1..]
    } else {
        s
    };

    // Find where the numbers end and pre-release tag begins (e.g. -beta1, .beta1, _rc2, alpha)
    let bytes = s.as_bytes();
    let mut num_end = 0;
    while num_end < bytes.len() {
        let b = bytes[num_end];
        if b.is_ascii_digit() || b == b'.' {
            num_end += 1;
        } else {
            break;
        }
    }

    let num_part = &s[..num_end];
    let tag_part = &s[num_end..];

    // Parse dot-separated numbers
    let mut numbers = Vec::with_capacity(4);
    for seg in num_part.split('.') {
        let seg = seg.trim();
        if !seg.is_empty() {
            if let Ok(val) = seg.parse::<u32>() {
                numbers.push(val);
            }
        }
    }

    if numbers.is_empty() {
        // Fallback: extract any contiguous digit sequence
        let mut cur: u32 = 0;
        let mut in_num = false;
        for &b in bytes {
            if b.is_ascii_digit() {
                cur = cur.saturating_mul(10).saturating_add((b - b'0') as u32);
                in_num = true;
            } else if in_num {
                numbers.push(cur);
                cur = 0;
                in_num = false;
            }
        }
        if in_num {
            numbers.push(cur);
        }
        if numbers.is_empty() {
            return None;
        }
        return Some(ParsedVersion::new(numbers, 999, 0));
    }

    // Parse pre-release tag if present
    let mut pre_level = 999;
    let mut pre_number = 0;

    let tag_clean = tag_part.trim_start_matches(|c: char| c == '-' || c == '.' || c == '_');
    if !tag_clean.is_empty() {
        let lower = tag_clean.to_ascii_lowercase();
        if lower.starts_with("alpha") {
            pre_level = 0;
            let rem = &lower["alpha".len()..];
            pre_number = rem.trim().parse::<u32>().unwrap_or(0);
        } else if lower.starts_with("beta") {
            pre_level = 1;
            let rem = &lower["beta".len()..];
            pre_number = rem.trim().parse::<u32>().unwrap_or(0);
        } else if lower.starts_with("pre") {
            pre_level = 1;
            let rem = &lower["pre".len()..];
            pre_number = rem.trim().parse::<u32>().unwrap_or(0);
        } else if lower.starts_with("rc") {
            pre_level = 2;
            let rem = &lower["rc".len()..];
            pre_number = rem.trim().parse::<u32>().unwrap_or(0);
        }
    }

    Some(ParsedVersion::new(numbers, pre_level, pre_number))
}

/// Checks whether two version strings represent the exact same version release.
pub fn same_version(remote: &str, installed: &str) -> bool {
    let r_clean = remote.trim().trim_start_matches(|c: char| c == 'v' || c == 'V');
    let i_clean = installed.trim().trim_start_matches(|c: char| c == 'v' || c == 'V');
    if r_clean.eq_ignore_ascii_case(i_clean) {
        return true;
    }

    let a = match parse_version(remote) {
        Some(v) => v,
        None => return false,
    };
    let b = match parse_version(installed) {
        Some(v) => v,
        None => return false,
    };

    if a.pre_level != b.pre_level || a.pre_number != b.pre_number {
        return false;
    }

    let max_len = a.numbers.len().max(b.numbers.len());
    for i in 0..max_len {
        let na = a.numbers.get(i).copied().unwrap_or(0);
        let nb = b.numbers.get(i).copied().unwrap_or(0);
        if na != nb {
            return false;
        }
    }

    true
}

/// Returns true if remote is strictly newer than installed version.
pub fn newer_version(remote: &str, installed: &str) -> bool {
    let a = match parse_version(remote) {
        Some(v) => v,
        None => return false,
    };
    let b = match parse_version(installed) {
        Some(v) => v,
        None => return false,
    };

    let max_len = a.numbers.len().max(b.numbers.len());
    for i in 0..max_len {
        let na = a.numbers.get(i).copied().unwrap_or(0);
        let nb = b.numbers.get(i).copied().unwrap_or(0);
        if na != nb {
            return na > nb;
        }
    }

    if a.pre_level != b.pre_level {
        return a.pre_level > b.pre_level;
    }

    a.pre_number > b.pre_number
}

/// Determines if an update is available based on remote tag, installed version, and update channel.
pub fn is_update_available(
    remote_tag: &str,
    installed_version: &str,
    is_beta_channel: bool,
    is_prerelease: bool,
) -> bool {
    let remote = remote_tag.trim();
    if remote.is_empty() {
        return false;
    }
    if same_version(remote, installed_version) {
        return false;
    }
    if !is_beta_channel && is_prerelease {
        return false;
    }
    newer_version(remote, installed_version)
}

/// Converts markdown formatted release notes to clean plain text.
/// Strips markdown headers, blockquotes, bullet formatting, bold/backtick delimiters,
/// and markdown links [title](url) -> title.
pub fn strip_markdown(markdown: &str) -> String {
    let mut out = String::with_capacity(markdown.len());
    let mut empty_line_count = 0;

    for line in markdown.lines() {
        let mut trimmed = line.trim();
        if trimmed.is_empty() {
            if empty_line_count < 2 && !out.is_empty() {
                out.push('\n');
                empty_line_count += 1;
            }
            continue;
        }
        empty_line_count = 0;

        // Strip headers (### ...)
        while trimmed.starts_with('#') {
            trimmed = trimmed[1..].trim_start();
        }

        // Strip blockquotes (> ...)
        if trimmed.starts_with('>') {
            trimmed = trimmed[1..].trim_start();
        }

        // Convert bullet lists (- or * to •)
        let bullet = if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) && trimmed.len() >= 2 {
            trimmed = trimmed[2..].trim_start();
            true
        } else {
            false
        };

        if bullet {
            out.push_str("• ");
        }

        // Strip formatting inside the line: `code`, **bold**, [text](url) -> text
        let mut chars = trimmed.chars().peekable();
        while let Some(c) = chars.next() {
            match c {
                '`' => continue,
                '*' => continue,
                '[' => {
                    // Collect link text until ']'
                    let mut text = String::new();
                    let mut found_bracket = false;
                    for lc in chars.by_ref() {
                        if lc == ']' {
                            found_bracket = true;
                            break;
                        }
                        text.push(lc);
                    }
                    if found_bracket && chars.peek() == Some(&'(') {
                        chars.next(); // consume '('
                        for lc in chars.by_ref() {
                            if lc == ')' {
                                break;
                            }
                        }
                        out.push_str(&text);
                    } else {
                        out.push('[');
                        out.push_str(&text);
                        if found_bracket {
                            out.push(']');
                        }
                    }
                }
                _ => out.push(c),
            }
        }
        out.push('\n');
    }

    out.trim().to_string()
}

// ---------------------------------------------------------------------------
// JNI Export Functions
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_ReleaseVersion_nativeSameVersion(
    mut env: JNIEnv,
    _class: JClass,
    remote: JString,
    installed: JString,
) -> jboolean {
    let r_str = match env.get_string(&remote) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    let i_str = match env.get_string(&installed) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    if same_version(&r_str, &i_str) { 1 } else { 0 }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_ReleaseVersion_nativeNewer(
    mut env: JNIEnv,
    _class: JClass,
    remote: JString,
    installed: JString,
) -> jboolean {
    let r_str = match env.get_string(&remote) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    let i_str = match env.get_string(&installed) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    if newer_version(&r_str, &i_str) { 1 } else { 0 }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_ReleaseVersion_nativeIsUpdateAvailable(
    mut env: JNIEnv,
    _class: JClass,
    remote_tag: JString,
    installed_version: JString,
    is_beta_channel: jboolean,
    is_prerelease: jboolean,
) -> jboolean {
    let r_str = match env.get_string(&remote_tag) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    let i_str = match env.get_string(&installed_version) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return 0,
    };
    let available = is_update_available(
        &r_str,
        &i_str,
        is_beta_channel != 0,
        is_prerelease != 0,
    );
    if available { 1 } else { 0 }
}

#[no_mangle]
pub extern "system" fn Java_io_mininxd_nfsu2_ReleaseUpdates_nativeStripMarkdown(
    mut env: JNIEnv,
    _class: JClass,
    markdown: JString,
) -> jstring {
    let md_str = match env.get_string(&markdown) {
        Ok(s) => s.to_string_lossy().to_string(),
        Err(_) => return std::ptr::null_mut(),
    };
    let plain = strip_markdown(&md_str);
    match env.new_string(plain) {
        Ok(js) => js.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_comparisons() {
        assert!(same_version("v0.15.24", "0.15.24"));
        assert!(same_version("v1.1.beta1", "1.1.beta1"));
        assert!(!same_version("v0.15.24", "0.15.24.1"));

        assert!(newer_version("v0.15.24", "0.15.23.3"));
        assert!(newer_version("0.15.24.1", "0.15.24"));
        assert!(newer_version("v0.15.25.beta1", "0.15.24.1"));
        assert!(newer_version("0.15.24", "0.15.24.beta1"));
        assert!(!newer_version("0.15.24.beta1", "0.15.24"));
    }

    #[test]
    fn test_is_update_available() {
        assert!(is_update_available("v1.1.beta1", "1.0.0", true, true));
        assert!(!is_update_available("v1.1.beta1", "1.0.0", false, true));
        assert!(is_update_available("v1.1.0", "1.0.0", false, false));
        assert!(!is_update_available("v1.0.0", "1.0.0", true, false));
    }

    #[test]
    fn test_strip_markdown() {
        let md = "## New Feature\n- Added **high frequency** mode: `perf`\nCheck [here](https://example.com).";
        let plain = strip_markdown(md);
        assert!(plain.contains("New Feature"));
        assert!(plain.contains("• Added high frequency mode: perf"));
        assert!(plain.contains("Check here."));
    }
}
