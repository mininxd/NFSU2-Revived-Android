//! Real-Time Audio Mixer & Resampling Acceleration in Rust for NFSU2 Android.
//!
//! Provides batch voice sample decoding, fast-path linear stereo resampling, and
//! vectorized NEON peak/clipping statistics for AAudio real-time output.

#[cfg(target_arch = "aarch64")]
use core::arch::aarch64::*;

#[inline(always)]
unsafe fn read_pcm_sample(samples: *const u8, block_stride: usize, bits: u32, frame: usize, channel: usize) -> f32 {
    let offset = frame * block_stride + channel * (bits as usize / 8);
    if bits == 8 {
        let byte = *samples.add(offset);
        (byte as i32 - 128) as f32 * (1.0 / 128.0)
    } else {
        let low = *samples.add(offset) as u16;
        let high = *samples.add(offset + 1) as u16;
        let word = (low | (high << 8)) as i16;
        word as f32 * (1.0 / 32768.0)
    }
}

/// Voice parameters descriptor for the audio mixing engine.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct VoiceDesc {
    pub samples: *const u8,
    pub bits: u32,
    pub channels: u32,
    pub total_frames: usize,
    pub position: f64,
    pub step: f64,
    pub loop_mode: i32,
    pub gain_l: f32,
    pub gain_r: f32,
}

/// Renders and accumulates a voice's audio into the stereo output buffer.
///
/// Dispatches to ARM64 NEON 16-tap sinc kernel upsampling if step < 1.0,
/// or high-throughput linear interpolation if step >= 1.0.
///
/// Returns 1 if the voice is still playing, or 0 if it has completed.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_mixer_render_voice(
    output: *mut f32,
    frames: u32,
    desc: *const VoiceDesc,
    kernel_weights: *const f32,
    num_phases: u32,
    num_taps: u32,
    pos_out: *mut f64,
) -> i32 {
    if output.is_null() || desc.is_null() || frames == 0 {
        return 0;
    }

    let d = &*desc;
    if d.samples.is_null() || d.total_frames == 0 || d.channels == 0 || (d.bits != 8 && d.bits != 16) {
        if !pos_out.is_null() {
            *pos_out = d.position;
        }
        return 0;
    }

    if d.step >= 1.0 || kernel_weights.is_null() || num_taps != 16 || num_phases == 0 {
        return nfs_audio_mixer_render_linear(
            output,
            frames,
            d.samples,
            d.bits,
            d.channels,
            d.total_frames,
            d.position,
            d.step,
            d.loop_mode,
            d.gain_l,
            d.gain_r,
            pos_out,
        );
    }

    let block_stride = (d.channels * (d.bits / 8)) as usize;
    let total_frames = d.total_frames;
    let count_i64 = total_frames as i64;
    let mut pos = d.position;
    let step = d.step;
    let loop_mode = d.loop_mode != 0;
    let gain_l = d.gain_l;
    let gain_r = d.gain_r;
    let samples = d.samples;
    let bits = d.bits;
    let ch1_idx = if d.channels > 1 { 1 } else { 0 };
    let mut still_playing = 1;

    for frame in 0..frames as usize {
        if pos >= total_frames as f64 {
            if loop_mode {
                pos %= total_frames as f64;
            } else {
                still_playing = 0;
                pos = 0.0;
                break;
            }
        }

        let a = pos as usize;
        let fraction = (pos - a as f64) as f32;
        let phase = fraction * num_phases as f32;
        let index = (phase as usize).min(num_phases as usize - 1);
        let blend = phase - index as f32;

        let weights_curr = kernel_weights.add(index * 16);
        let weights_next = kernel_weights.add((index + 1) * 16);

        let mut samples_l = [0.0f32; 16];
        let mut samples_r = [0.0f32; 16];

        if a >= 7 && a + 8 < total_frames {
            let start_src = a - 7;
            for tap in 0..16 {
                let src = start_src + tap;
                samples_l[tap] = read_pcm_sample(samples, block_stride, bits, src, 0);
                samples_r[tap] = read_pcm_sample(samples, block_stride, bits, src, ch1_idx);
            }
        } else {
            for tap in 0..16 {
                let mut source = a as i64 + tap as i64 - 7;
                if source < 0 || source >= count_i64 {
                    if loop_mode {
                        source %= count_i64;
                        if source < 0 {
                            source += count_i64;
                        }
                    } else {
                        source = source.clamp(0, count_i64 - 1);
                    }
                }
                let src = source as usize;
                samples_l[tap] = read_pcm_sample(samples, block_stride, bits, src, 0);
                samples_r[tap] = read_pcm_sample(samples, block_stride, bits, src, ch1_idx);
            }
        }

        let (l, r) = crate::simd_audio::resample_sinc_16tap_neon(
            samples_l.as_ptr(),
            samples_r.as_ptr(),
            weights_curr,
            weights_next,
            blend,
        );

        let out_idx = frame * 2;
        *output.add(out_idx) += l * gain_l;
        *output.add(out_idx + 1) += r * gain_r;

        pos += step;
    }

    if !pos_out.is_null() {
        *pos_out = pos;
    }

    still_playing
}

/// Renders and accumulates a voice's samples into the stereo output buffer using linear interpolation.
///
/// Returns 1 if the voice is still playing, or 0 if it has completed.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_mixer_render_linear(
    output: *mut f32,
    frames: u32,
    samples: *const u8,
    bits: u32,
    channels: u32,
    total_frames: usize,
    mut pos: f64,
    step: f64,
    loop_mode: i32,
    gain_l: f32,
    gain_r: f32,
    pos_out: *mut f64,
) -> i32 {
    if output.is_null() || samples.is_null() || total_frames == 0 || frames == 0 {
        return 0;
    }

    let block_stride = (channels * (bits / 8)) as usize;
    let mut still_playing = 1;

    for frame in 0..frames as usize {
        if pos >= total_frames as f64 {
            if loop_mode != 0 {
                pos %= total_frames as f64;
            } else {
                still_playing = 0;
                pos = 0.0;
                break;
            }
        }

        let a = pos as usize;
        let mut b = a + 1;
        let fraction = (pos - a as f64) as f32;

        if b >= total_frames {
            b = if loop_mode != 0 { 0 } else { a };
        }

        let ch0_idx = 0;
        let ch1_idx = if channels > 1 { 1 } else { 0 };

        let first_l = read_pcm_sample(samples, block_stride, bits, a, ch0_idx);
        let second_l = read_pcm_sample(samples, block_stride, bits, b, ch0_idx);
        let val_l = first_l + (second_l - first_l) * fraction;

        let first_r = read_pcm_sample(samples, block_stride, bits, a, ch1_idx);
        let second_r = read_pcm_sample(samples, block_stride, bits, b, ch1_idx);
        let val_r = first_r + (second_r - first_r) * fraction;

        let out_idx = frame * 2;
        *output.add(out_idx) += val_l * gain_l;
        *output.add(out_idx + 1) += val_r * gain_r;

        pos += step;
    }

    if !pos_out.is_null() {
        *pos_out = pos;
    }

    still_playing
}

/// Applies vector soft-clipping [-1.0, 1.0] and computes peak amplitude and clipped sample counts.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_mixer_clamp_and_stats(
    output: *mut f32,
    total_samples: usize,
    out_peak: *mut f32,
    out_clipped: *mut u64,
) {
    if output.is_null() || total_samples == 0 {
        return;
    }

    let mut peak = 0.0f32;
    let mut clipped = 0u64;

    #[cfg(target_arch = "aarch64")]
    {
        let v_neg_one = vdupq_n_f32(-1.0);
        let v_pos_one = vdupq_n_f32(1.0);
        let mut v_peak = vdupq_n_f32(0.0);

        let chunks = total_samples / 4;
        let remainder = total_samples % 4;

        for i in 0..chunks {
            let ptr = output.add(i * 4);
            let v = vld1q_f32(ptr);
            let v_abs = vabsq_f32(v);
            v_peak = vmaxq_f32(v_peak, v_abs);

            // Clamp: max(-1.0, min(1.0, v))
            let v_clamped = vmaxq_f32(v_neg_one, vminq_f32(v_pos_one, v));
            vst1q_f32(ptr, v_clamped);
        }

        let mut peak_lanes = [0.0f32; 4];
        vst1q_f32(peak_lanes.as_mut_ptr(), v_peak);
        peak = peak_lanes[0].max(peak_lanes[1]).max(peak_lanes[2]).max(peak_lanes[3]);

        for i in 0..(chunks * 4) {
            let s = *output.add(i);
            if s >= 1.0 || s <= -1.0 {
                clipped += 1;
            }
        }

        // Handle remainder
        for i in (chunks * 4)..total_samples {
            let s = *output.add(i);
            let abs_s = s.abs();
            if abs_s > peak {
                peak = abs_s;
            }
            if abs_s > 1.0 {
                clipped += 1;
                *output.add(i) = s.clamp(-1.0, 1.0);
            }
        }
    }

    #[cfg(not(target_arch = "aarch64"))]
    {
        for i in 0..total_samples {
            let s = *output.add(i);
            let abs_s = s.abs();
            if abs_s > peak {
                peak = abs_s;
            }
            if abs_s > 1.0 {
                clipped += 1;
                *output.add(i) = s.clamp(-1.0, 1.0);
            }
        }
    }

    if !out_peak.is_null() {
        *out_peak = peak;
    }
    if !out_clipped.is_null() {
        *out_clipped += clipped;
    }
}

/// Batch calculates 3D audio attenuation and stereo panning for sound emitters.
#[no_mangle]
pub unsafe extern "C" fn nfs_audio_3d_positional_batch(
    count: usize,
    emitters_xyz: *const f32, // count * 3
    listener_pos: *const f32, // 3
    listener_fwd: *const f32, // 3
    listener_rgt: *const f32, // 3
    min_dist: f32,
    max_dist: f32,
    out_gains_lr: *mut f32,   // count * 2 (gain_l, gain_r)
) {
    if emitters_xyz.is_null() || listener_pos.is_null() || listener_rgt.is_null() || out_gains_lr.is_null() || count == 0 {
        return;
    }

    let lx = *listener_pos;
    let ly = *listener_pos.add(1);
    let lz = *listener_pos.add(2);

    let rx = *listener_rgt;
    let ry = *listener_rgt.add(1);
    let rz = *listener_rgt.add(2);

    let dist_range = (max_dist - min_dist).max(0.001);

    for i in 0..count {
        let ex = *emitters_xyz.add(i * 3);
        let ey = *emitters_xyz.add(i * 3 + 1);
        let ez = *emitters_xyz.add(i * 3 + 2);

        let dx = ex - lx;
        let dy = ey - ly;
        let dz = ez - lz;

        let dist_sq = dx * dx + dy * dy + dz * dz;
        let dist = dist_sq.sqrt();

        // Linear roll-off clamped between min_dist and max_dist
        let atten = if dist <= min_dist {
            1.0
        } else if dist >= max_dist {
            0.0
        } else {
            (max_dist - dist) / dist_range
        };

        // Panning dot product with listener right vector
        let pan = if dist > 0.001 {
            let inv_d = 1.0 / dist;
            let ndx = dx * inv_d;
            let ndy = dy * inv_d;
            let ndz = dz * inv_d;
            (ndx * rx + ndy * ry + ndz * rz).clamp(-1.0, 1.0)
        } else {
            0.0
        };

        let gain_l = atten * (0.5 - 0.5 * pan);
        let gain_r = atten * (0.5 + 0.5 * pan);

        *out_gains_lr.add(i * 2) = gain_l;
        *out_gains_lr.add(i * 2 + 1) = gain_r;
    }
}

#[repr(C)]
pub struct VoiceAdvanceState {
    pub position: f64,
    pub stale_frames: f64,
    pub step: f64,
    pub count: usize,
    pub loop_mode: u32,
    pub is_streaming: u32,
    pub is_playing: u32,
}

#[no_mangle]
pub unsafe extern "C" fn nfs_audio_mixer_advance_voices(
    voices: *mut VoiceAdvanceState,
    voice_count: usize,
    frames: u32,
) {
    if voices.is_null() || voice_count == 0 || frames == 0 {
        return;
    }
    let frames_f = frames as f64;
    for i in 0..voice_count {
        let v = &mut *voices.add(i);
        if v.is_playing == 0 || v.count == 0 {
            continue;
        }
        let total_frames = v.count as f64;
        let delta = v.step * frames_f;
        if v.is_streaming != 0 && v.stale_frames >= total_frames {
            v.position = (v.position + delta) % total_frames;
            continue;
        }
        v.stale_frames += delta;
        v.position += delta;
        if v.position >= total_frames {
            if v.loop_mode != 0 {
                v.position %= total_frames;
            } else {
                v.is_playing = 0;
                v.position = 0.0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Pure Rust 128-Voice Audio Mixer Engine
// ---------------------------------------------------------------------------

pub const MAX_VOICES: usize = 128;

#[derive(Clone, Debug)]
pub struct NativeVoice {
    pub samples: Vec<u8>,
    pub channels: u32,
    pub bits: u32,
    pub rate: u32,
    pub frequency: u32,
    pub position: f64,
    pub gain_l: f32,
    pub gain_r: f32,
    pub is_playing: bool,
    pub is_loop: bool,
    pub publishes: u32,
    pub stale_frames: f64,
}

impl NativeVoice {
    pub fn new(
        samples: Vec<u8>,
        channels: u32,
        bits: u32,
        rate: u32,
        frequency: u32,
        is_loop: bool,
        gain_l: f32,
        gain_r: f32,
    ) -> Self {
        Self {
            samples,
            channels,
            bits,
            rate,
            frequency,
            position: 0.0,
            gain_l,
            gain_r,
            is_playing: true,
            is_loop,
            publishes: 0,
            stale_frames: 0.0,
        }
    }

    #[inline]
    pub fn block_stride(&self) -> usize {
        (self.channels * (self.bits / 8)) as usize
    }

    #[inline]
    pub fn total_frames(&self) -> usize {
        let stride = self.block_stride();
        if stride == 0 {
            0
        } else {
            self.samples.len() / stride
        }
    }

    #[inline]
    pub fn is_streaming(&self) -> bool {
        self.is_loop && self.publishes >= 4
    }
}

pub struct AudioMixer {
    pub voices: [Option<NativeVoice>; MAX_VOICES],
    pub sample_rate: u32,
    pub clipped_samples: u64,
    pub peak_amplitude: f32,
}

impl AudioMixer {
    pub const fn new() -> Self {
        const NONE_VOICE: Option<NativeVoice> = None;
        Self {
            voices: [NONE_VOICE; MAX_VOICES],
            sample_rate: 48000,
            clipped_samples: 0,
            peak_amplitude: 0.0,
        }
    }

    pub fn add_voice(&mut self, voice: NativeVoice) -> Option<usize> {
        for (i, slot) in self.voices.iter_mut().enumerate() {
            if slot.is_none() {
                *slot = Some(voice);
                return Some(i);
            }
        }
        None
    }

    pub fn remove_voice(&mut self, slot_idx: usize) {
        if slot_idx < MAX_VOICES {
            self.voices[slot_idx] = None;
        }
    }

    pub fn render(&mut self, output: &mut [f32]) {
        output.fill(0.0);
        let frames = (output.len() / 2) as u32;
        if frames == 0 {
            return;
        }

        for slot in self.voices.iter_mut() {
            if let Some(voice) = slot {
                if !voice.is_playing || voice.samples.is_empty() {
                    continue;
                }
                let total = voice.total_frames();
                if total == 0 {
                    continue;
                }
                let step = voice.frequency as f64 / self.sample_rate as f64;
                if voice.is_streaming() && voice.stale_frames >= total as f64 {
                    voice.position = (voice.position + step * frames as f64) % (total as f64);
                    continue;
                }
                voice.stale_frames += step * frames as f64;

                let mut next_pos = voice.position;
                let desc = VoiceDesc {
                    samples: voice.samples.as_ptr(),
                    bits: voice.bits,
                    channels: voice.channels,
                    total_frames: total,
                    position: voice.position,
                    step,
                    loop_mode: if voice.is_loop { 1 } else { 0 },
                    gain_l: voice.gain_l,
                    gain_r: voice.gain_r,
                };

                let still = unsafe {
                    nfs_audio_mixer_render_voice(
                        output.as_mut_ptr(),
                        frames,
                        &desc,
                        std::ptr::null(),
                        0,
                        0,
                        &mut next_pos,
                    )
                };
                voice.position = next_pos;
                if still == 0 {
                    voice.is_playing = false;
                }
            }
        }

        unsafe {
            nfs_audio_mixer_clamp_and_stats(
                output.as_mut_ptr(),
                output.len(),
                &mut self.peak_amplitude,
                &mut self.clipped_samples,
            );
        }
    }
}

static AUDIO_ENGINE_GLOBAL: std::sync::Mutex<AudioMixer> = std::sync::Mutex::new(AudioMixer::new());

#[no_mangle]
pub unsafe extern "C" fn nfs_audio_engine_render(output: *mut f32, frames: u32) {
    if output.is_null() || frames == 0 {
        return;
    }
    let slice = std::slice::from_raw_parts_mut(output, (frames * 2) as usize);
    let mut engine = AUDIO_ENGINE_GLOBAL.lock().unwrap();
    engine.render(slice);
}

#[no_mangle]
pub unsafe extern "C" fn nfs_audio_engine_get_peak() -> f32 {
    let engine = AUDIO_ENGINE_GLOBAL.lock().unwrap();
    engine.peak_amplitude
}

#[no_mangle]
pub unsafe extern "C" fn nfs_audio_engine_get_clipped() -> u64 {
    let engine = AUDIO_ENGINE_GLOBAL.lock().unwrap();
    engine.clipped_samples
}

