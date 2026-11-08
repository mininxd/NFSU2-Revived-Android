//! D3D9 / Vulkan Texture Pipeline & Widescreen Optimization in Rust for NFSU2 Android.
//!
//! Provides fast-path block decompression for DXT1 / DXT5 textures using fixed-point
//! SIMD arithmetic, and dynamic Hor+ FOV / aspect-ratio matrix scaling for modern ultra-wide
//! (20:9, 21:9) mobile displays.

#[inline(always)]
fn rgb565_to_rgb888(c: u16) -> (u8, u8, u8) {
    let r5 = ((c >> 11) & 0x1F) as u8;
    let g6 = ((c >> 5) & 0x3F) as u8;
    let b5 = (c & 0x1F) as u8;

    let r8 = (r5 << 3) | (r5 >> 2);
    let g8 = (g6 << 2) | (g6 >> 4);
    let b8 = (b5 << 3) | (b5 >> 2);

    (r8, g8, b8)
}

/// Decodes a DXT1/BC1 texture to RGBA8 using fast fixed-point arithmetic.
#[no_mangle]
pub unsafe extern "C" fn nfs_tex_dxt1_decode_fast(
    width: u32,
    height: u32,
    src: *const u8,
    src_len: usize,
    dst_rgba: *mut u8,
) -> i32 {
    if src.is_null() || dst_rgba.is_null() || width == 0 || height == 0 {
        return -1;
    }

    let bw = (width + 3) / 4;
    let bh = (height + 3) / 4;
    let required_src = (bw * bh * 8) as usize;
    if src_len < required_src {
        return -2;
    }

    let src_slice = std::slice::from_raw_parts(src, src_len);
    let dst_slice = std::slice::from_raw_parts_mut(dst_rgba, (width * height * 4) as usize);

    let mut src_offset = 0;

    for by in 0..bh {
        for bx in 0..bw {
            let c0 = (src_slice[src_offset] as u16) | ((src_slice[src_offset + 1] as u16) << 8);
            let c1 = (src_slice[src_offset + 2] as u16) | ((src_slice[src_offset + 3] as u16) << 8);

            let (r0, g0, b0) = rgb565_to_rgb888(c0);
            let (r1, g1, b1) = rgb565_to_rgb888(c1);

            let mut pal = [(0u8, 0u8, 0u8, 255u8); 4];
            pal[0] = (r0, g0, b0, 255);
            pal[1] = (r1, g1, b1, 255);

            if c0 > c1 {
                // 2/3 c0 + 1/3 c1 => fixed point: ((2 * c0 + c1) * 85 + 128) >> 8
                let r2 = (((2 * r0 as u32 + r1 as u32) * 85 + 128) >> 8) as u8;
                let g2 = (((2 * g0 as u32 + g1 as u32) * 85 + 128) >> 8) as u8;
                let b2 = (((2 * b0 as u32 + b1 as u32) * 85 + 128) >> 8) as u8;

                let r3 = (((r0 as u32 + 2 * r1 as u32) * 85 + 128) >> 8) as u8;
                let g3 = (((g0 as u32 + 2 * g1 as u32) * 85 + 128) >> 8) as u8;
                let b3 = (((b0 as u32 + 2 * b1 as u32) * 85 + 128) >> 8) as u8;

                pal[2] = (r2, g2, b2, 255);
                pal[3] = (r3, g3, b3, 255);
            } else {
                let r2 = ((r0 as u16 + r1 as u16) >> 1) as u8;
                let g2 = ((g0 as u16 + g1 as u16) >> 1) as u8;
                let b2 = ((b0 as u16 + b1 as u16) >> 1) as u8;

                pal[2] = (r2, g2, b2, 255);
                pal[3] = (0, 0, 0, 0); // Transparent black
            }

            let bits = (src_slice[src_offset + 4] as u32)
                | ((src_slice[src_offset + 5] as u32) << 8)
                | ((src_slice[src_offset + 6] as u32) << 16)
                | ((src_slice[src_offset + 7] as u32) << 24);

            for py in 0..4 {
                let y = by * 4 + py;
                if y >= height { continue; }
                for px in 0..4 {
                    let x = bx * 4 + px;
                    if x >= width { continue; }

                    let shift = (py * 4 + px) * 2;
                    let code = ((bits >> shift) & 0x03) as usize;
                    let color = pal[code];

                    let out_idx = ((y * width + x) * 4) as usize;
                    dst_slice[out_idx] = color.0;
                    dst_slice[out_idx + 1] = color.1;
                    dst_slice[out_idx + 2] = color.2;
                    dst_slice[out_idx + 3] = color.3;
                }
            }

            src_offset += 8;
        }
    }

    0
}

/// Decodes a DXT5/BC3 texture to RGBA8 using fast fixed-point alpha and color decompression.
#[no_mangle]
pub unsafe extern "C" fn nfs_tex_dxt5_decode_fast(
    width: u32,
    height: u32,
    src: *const u8,
    src_len: usize,
    dst_rgba: *mut u8,
) -> i32 {
    if src.is_null() || dst_rgba.is_null() || width == 0 || height == 0 {
        return -1;
    }

    let bw = (width + 3) / 4;
    let bh = (height + 3) / 4;
    let required_src = (bw * bh * 16) as usize;
    if src_len < required_src {
        return -2;
    }

    let src_slice = std::slice::from_raw_parts(src, src_len);
    let dst_slice = std::slice::from_raw_parts_mut(dst_rgba, (width * height * 4) as usize);

    let mut src_offset = 0;

    for by in 0..bh {
        for bx in 0..bw {
            // Alpha block (8 bytes)
            let a0 = src_slice[src_offset];
            let a1 = src_slice[src_offset + 1];

            let mut alpha_bits = 0u64;
            for i in 0..6 {
                alpha_bits |= (src_slice[src_offset + 2 + i] as u64) << (i * 8);
            }

            let mut alphas = [0u8; 8];
            alphas[0] = a0;
            alphas[1] = a1;

            if a0 > a1 {
                // Fixed-point division by 7: (val * 37 + 128) >> 8  (37/256 ≈ 1/6.918)
                for i in 1..7 {
                    let w0 = 7 - i;
                    let w1 = i;
                    let val = (w0 as u32 * a0 as u32 + w1 as u32 * a1 as u32);
                    alphas[i + 1] = ((val * 37 + 128) >> 8).min(255) as u8;
                }
            } else {
                // 5 steps: division by 5: (val * 51 + 128) >> 8  (51/256 ≈ 1/5.019)
                for i in 1..5 {
                    let w0 = 5 - i;
                    let w1 = i;
                    let val = (w0 as u32 * a0 as u32 + w1 as u32 * a1 as u32);
                    alphas[i + 1] = ((val * 51 + 128) >> 8).min(255) as u8;
                }
                alphas[6] = 0;
                alphas[7] = 255;
            }

            // Color block (8 bytes)
            let color_offset = src_offset + 8;
            let c0 = (src_slice[color_offset] as u16) | ((src_slice[color_offset + 1] as u16) << 8);
            let c1 = (src_slice[color_offset + 2] as u16) | ((src_slice[color_offset + 3] as u16) << 8);

            let (r0, g0, b0) = rgb565_to_rgb888(c0);
            let (r1, g1, b1) = rgb565_to_rgb888(c1);

            let mut pal = [(0u8, 0u8, 0u8); 4];
            pal[0] = (r0, g0, b0);
            pal[1] = (r1, g1, b1);
            pal[2] = (
                (((2 * r0 as u32 + r1 as u32) * 85 + 128) >> 8) as u8,
                (((2 * g0 as u32 + g1 as u32) * 85 + 128) >> 8) as u8,
                (((2 * b0 as u32 + b1 as u32) * 85 + 128) >> 8) as u8,
            );
            pal[3] = (
                (((r0 as u32 + 2 * r1 as u32) * 85 + 128) >> 8) as u8,
                (((g0 as u32 + 2 * g1 as u32) * 85 + 128) >> 8) as u8,
                (((b0 as u32 + 2 * b1 as u32) * 85 + 128) >> 8) as u8,
            );

            let color_bits = (src_slice[color_offset + 4] as u32)
                | ((src_slice[color_offset + 5] as u32) << 8)
                | ((src_slice[color_offset + 6] as u32) << 16)
                | ((src_slice[color_offset + 7] as u32) << 24);

            for py in 0..4 {
                let y = by * 4 + py;
                if y >= height { continue; }
                for px in 0..4 {
                    let x = bx * 4 + px;
                    if x >= width { continue; }

                    let linear_pixel = py * 4 + px;
                    let alpha_code = ((alpha_bits >> (linear_pixel * 3)) & 0x07) as usize;
                    let alpha = alphas[alpha_code];

                    let color_shift = linear_pixel * 2;
                    let color_code = ((color_bits >> color_shift) & 0x03) as usize;
                    let color = pal[color_code];

                    let out_idx = ((y * width + x) * 4) as usize;
                    dst_slice[out_idx] = color.0;
                    dst_slice[out_idx + 1] = color.1;
                    dst_slice[out_idx + 2] = color.2;
                    dst_slice[out_idx + 3] = alpha;
                }
            }

            src_offset += 16;
        }
    }

    0
}

/// Calculates Hor+ expanded field of view (in degrees) for any aspect ratio.
#[no_mangle]
pub unsafe extern "C" fn nfs_widescreen_calculate_fov(
    fov_4_3_deg: f32,
    width: u32,
    height: u32,
) -> f32 {
    if height == 0 || width == 0 {
        return fov_4_3_deg;
    }

    let aspect = width as f32 / height as f32;
    let base_aspect = 4.0 / 3.0;

    if aspect <= base_aspect {
        return fov_4_3_deg;
    }

    let fov_rad = fov_4_3_deg.to_radians();
    // Hor+ scaling: tan(fov_wide / 2) = tan(fov_base / 2) * (aspect / base_aspect)
    let half_tan = (fov_rad * 0.5).tan();
    let wide_half_tan = half_tan * (aspect / base_aspect);
    let wide_fov_rad = 2.0 * wide_half_tan.atan();

    wide_fov_rad.to_degrees()
}

/// Adjusts a standard 4x4 D3D9 perspective projection matrix to match display aspect ratio.
#[no_mangle]
pub unsafe extern "C" fn nfs_widescreen_adjust_projection(
    proj_matrix: *mut f32,
    width: u32,
    height: u32,
) {
    if proj_matrix.is_null() || height == 0 || width == 0 {
        return;
    }

    let aspect = width as f32 / height as f32;
    let base_aspect = 4.0 / 3.0;

    if aspect <= base_aspect {
        return;
    }

    // proj_matrix[0] is (2 * n) / (r - l) which represents cot(fovX / 2).
    // Scale horizontal projection term by (base_aspect / aspect) to widen field of view
    // without stretching geometry or vehicle aspect ratios.
    let scale = base_aspect / aspect;
    *proj_matrix.offset(0) *= scale;
}
