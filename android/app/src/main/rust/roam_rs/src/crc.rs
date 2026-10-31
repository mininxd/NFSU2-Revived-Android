//! CRC-32 (IEEE 802.3 / zlib polynomial 0xEDB88320, reflected) calculation.

#[no_mangle]
pub extern "C" fn rc_crc32(p: *const u8, n: u32) -> u32 {
    if p.is_null() || n == 0 {
        return 0;
    }
    let slice = unsafe { std::slice::from_raw_parts(p, n as usize) };
    let mut c: u32 = 0xFFFF_FFFF;
    for &b in slice {
        c ^= b as u32;
        for _ in 0..8 {
            c = (c >> 1) ^ (0xEDB8_8320 & (0u32.wrapping_sub(c & 1)));
        }
    }
    !c
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc32() {
        let data = b"123456789";
        assert_eq!(rc_crc32(data.as_ptr(), data.len() as u32), 0xCBF43926);
    }
}
