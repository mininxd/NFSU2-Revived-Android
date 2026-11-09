//! DirectSound Buffer Cursor, Circular Lock Slices, and Volume/Pan Gain Calculator.
//!
//! Provides pure Rust implementations for DirectSound buffer cursor tracking,
//! circular ring-buffer split calculations (DSBLOCK_FROMWRITECURSOR, DSBLOCK_ENTIREBUFFER),
//! and logarithmic decibel attenuation conversions for audio voices.

#[inline(always)]
pub fn calc_cursors(
    position_frames: f64,
    block_align: u32,
    total_bytes: u32,
    sample_rate: u32,
) -> (u32, u32) {
    if total_bytes == 0 || block_align == 0 {
        return (0, 0);
    }
    let pos_bytes = ((position_frames as u64 * block_align as u64) as u32) % total_bytes;
    // Write cursor is positioned 10ms (sample_rate / 100) ahead of the play cursor
    let write_margin = (sample_rate / 100) * block_align;
    let write_bytes = (pos_bytes + write_margin) % total_bytes;
    (pos_bytes, write_bytes)
}

#[inline(always)]
pub fn calc_lock_slices(
    total_bytes: u32,
    mut offset: u32,
    mut length: u32,
    flags: u32,
    current_play_bytes: u32,
    sample_rate: u32,
    block_align: u32,
) -> Result<(u32, u32, u32), ()> {
    if total_bytes == 0 || (flags & !3) != 0 {
        return Err(());
    }

    // DSBLOCK_FROMWRITECURSOR = 1
    if (flags & 1) != 0 {
        let write_margin = (sample_rate / 100) * block_align;
        offset = (current_play_bytes + write_margin) % total_bytes;
    }

    // DSBLOCK_ENTIREBUFFER = 2
    if (flags & 2) != 0 {
        offset = 0;
        length = total_bytes;
    }

    if offset >= total_bytes || length > total_bytes {
        return Err(());
    }

    let first = (total_bytes - offset).min(length);
    let second = length - first;

    Ok((offset, first, second))
}

#[inline(always)]
pub fn calc_volume_pan_gains(volume_m_b: i32, pan_m_b: i32) -> (f32, f32) {
    let vol_clamped = volume_m_b.clamp(-10000, 0);
    let pan_clamped = pan_m_b.clamp(-10000, 10000);

    // DirectSound volume attenuation: 10^(vol / 2000.0)
    let gain = if vol_clamped <= -10000 {
        0.0f32
    } else {
        10.0f32.powf((vol_clamped as f32) / 2000.0)
    };

    let (left_att, right_att) = if pan_clamped > 0 {
        (10.0f32.powf((-pan_clamped as f32) / 2000.0), 1.0f32)
    } else if pan_clamped < 0 {
        (1.0f32, 10.0f32.powf((pan_clamped as f32) / 2000.0))
    } else {
        (1.0f32, 1.0f32)
    };

    (gain * left_att, gain * right_att)
}

// C-ABI Exports

#[no_mangle]
pub unsafe extern "C" fn nfs_dsound_calc_cursors(
    position_frames: f64,
    block_align: u32,
    total_bytes: u32,
    sample_rate: u32,
    out_play: *mut u32,
    out_write: *mut u32,
) {
    let (play, write) = calc_cursors(position_frames, block_align, total_bytes, sample_rate);
    if !out_play.is_null() {
        *out_play = play;
    }
    if !out_write.is_null() {
        *out_write = write;
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_dsound_calc_lock_slices(
    total_bytes: u32,
    offset: u32,
    length: u32,
    flags: u32,
    current_play_bytes: u32,
    sample_rate: u32,
    block_align: u32,
    out_offset: *mut u32,
    out_first_bytes: *mut u32,
    out_second_bytes: *mut u32,
) -> i32 {
    match calc_lock_slices(
        total_bytes,
        offset,
        length,
        flags,
        current_play_bytes,
        sample_rate,
        block_align,
    ) {
        Ok((resolved_off, first, second)) => {
            if !out_offset.is_null() {
                *out_offset = resolved_off;
            }
            if !out_first_bytes.is_null() {
                *out_first_bytes = first;
            }
            if !out_second_bytes.is_null() {
                *out_second_bytes = second;
            }
            0 // Success (DS_OK)
        }
        Err(_) => -1, // Error (DSERR_INVALIDPARAM)
    }
}

#[no_mangle]
pub unsafe extern "C" fn nfs_dsound_calc_volume_pan_gains(
    volume_m_b: i32,
    pan_m_b: i32,
    out_left: *mut f32,
    out_right: *mut f32,
) {
    let (left, right) = calc_volume_pan_gains(volume_m_b, pan_m_b);
    if !out_left.is_null() {
        *out_left = left;
    }
    if !out_right.is_null() {
        *out_right = right;
    }
}
