//! Direct3D 9 Vertex Declaration, FVF Decomposition, and Layout Hasher.
//!
//! Provides fast, validated parsing of `D3DVERTEXELEMENT9` structures and
//! decomposition of legacy Flexible Vertex Formats (FVF) into canonical
//! vertex stream layouts and 64-bit pipeline layout hashes.

#[repr(C)]
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct D3DVertexElement9 {
    pub stream: u16,
    pub offset: u16,
    pub element_type: u8,
    pub method: u8,
    pub usage: u8,
    pub usage_index: u8,
}

pub const D3DDECLTYPE_FLOAT1: u8 = 0;
pub const D3DDECLTYPE_FLOAT2: u8 = 1;
pub const D3DDECLTYPE_FLOAT3: u8 = 2;
pub const D3DDECLTYPE_FLOAT4: u8 = 3;
pub const D3DDECLTYPE_D3DCOLOR: u8 = 4;
pub const D3DDECLTYPE_UBYTE4: u8 = 5;
pub const D3DDECLTYPE_SHORT2: u8 = 6;
pub const D3DDECLTYPE_SHORT4: u8 = 7;
pub const D3DDECLTYPE_UBYTE4N: u8 = 8;
pub const D3DDECLTYPE_SHORT2N: u8 = 9;
pub const D3DDECLTYPE_SHORT4N: u8 = 10;
pub const D3DDECLTYPE_USHORT2N: u8 = 11;
pub const D3DDECLTYPE_USHORT4N: u8 = 12;
pub const D3DDECLTYPE_UDEC3: u8 = 13;
pub const D3DDECLTYPE_DEC3N: u8 = 14;
pub const D3DDECLTYPE_FLOAT16_2: u8 = 15;
pub const D3DDECLTYPE_FLOAT16_4: u8 = 16;
pub const D3DDECLTYPE_UNUSED: u8 = 17;

pub const D3DDECLUSAGE_POSITION: u8 = 0;
pub const D3DDECLUSAGE_BLENDWEIGHT: u8 = 1;
pub const D3DDECLUSAGE_BLENDINDICES: u8 = 2;
pub const D3DDECLUSAGE_NORMAL: u8 = 3;
pub const D3DDECLUSAGE_PSIZE: u8 = 4;
pub const D3DDECLUSAGE_TEXCOORD: u8 = 5;
pub const D3DDECLUSAGE_TANGENT: u8 = 6;
pub const D3DDECLUSAGE_BINORMAL: u8 = 7;
pub const D3DDECLUSAGE_TESSFACTOR: u8 = 8;
pub const D3DDECLUSAGE_POSITIONT: u8 = 9;
pub const D3DDECLUSAGE_COLOR: u8 = 10;
pub const D3DDECLUSAGE_FOG: u8 = 11;
pub const D3DDECLUSAGE_DEPTH: u8 = 12;
pub const D3DDECLUSAGE_SAMPLE: u8 = 13;

pub const D3DDECLMETHOD_DEFAULT: u8 = 0;
pub const D3DDECL_END: D3DVertexElement9 = D3DVertexElement9 {
    stream: 0xFF,
    offset: 0,
    element_type: D3DDECLTYPE_UNUSED,
    method: 0,
    usage: 0,
    usage_index: 0,
};

// FVF Flags
pub const D3DFVF_RESERVED0: u32 = 0x001;
pub const D3DFVF_POSITION_MASK: u32 = 0x400E;
pub const D3DFVF_XYZ: u32 = 0x002;
pub const D3DFVF_XYZRHW: u32 = 0x004;
pub const D3DFVF_XYZB1: u32 = 0x006;
pub const D3DFVF_XYZB2: u32 = 0x008;
pub const D3DFVF_XYZB3: u32 = 0x00a;
pub const D3DFVF_XYZB4: u32 = 0x00c;
pub const D3DFVF_XYZB5: u32 = 0x00e;
pub const D3DFVF_XYZW: u32 = 0x4002;
pub const D3DFVF_NORMAL: u32 = 0x010;
pub const D3DFVF_PSIZE: u32 = 0x020;
pub const D3DFVF_DIFFUSE: u32 = 0x040;
pub const D3DFVF_SPECULAR: u32 = 0x080;
pub const D3DFVF_TEXCOUNT_MASK: u32 = 0xf00;
pub const D3DFVF_TEXCOUNT_SHIFT: u32 = 8;

/// Returns the byte size of a D3DDECLTYPE.
#[inline(always)]
pub fn decl_type_size(decl_type: u8) -> u32 {
    match decl_type {
        D3DDECLTYPE_FLOAT1 => 4,
        D3DDECLTYPE_FLOAT2 => 8,
        D3DDECLTYPE_FLOAT3 => 12,
        D3DDECLTYPE_FLOAT4 => 16,
        D3DDECLTYPE_D3DCOLOR => 4,
        D3DDECLTYPE_UBYTE4 => 4,
        D3DDECLTYPE_SHORT2 => 4,
        D3DDECLTYPE_SHORT4 => 8,
        D3DDECLTYPE_UBYTE4N => 4,
        D3DDECLTYPE_SHORT2N => 4,
        D3DDECLTYPE_SHORT4N => 8,
        D3DDECLTYPE_USHORT2N => 4,
        D3DDECLTYPE_USHORT4N => 8,
        D3DDECLTYPE_UDEC3 => 4,
        D3DDECLTYPE_DEC3N => 4,
        D3DDECLTYPE_FLOAT16_2 => 4,
        D3DDECLTYPE_FLOAT16_4 => 8,
        _ => 0,
    }
}

/// Computes the stride for each stream (up to 16 streams) in a declaration array.
pub fn compute_stream_strides(elements: &[D3DVertexElement9], strides: &mut [u32; 16]) {
    strides.fill(0);
    for elem in elements {
        if elem.stream == 0xFF {
            break;
        }
        let stream_idx = elem.stream as usize;
        if stream_idx < 16 {
            let elem_end = (elem.offset as u32) + decl_type_size(elem.element_type);
            if elem_end > strides[stream_idx] {
                strides[stream_idx] = elem_end;
            }
        }
    }
}

/// Computes a fast 64-bit FNV-1a hash of a vertex declaration.
pub fn hash_vertex_declaration(elements: &[D3DVertexElement9]) -> u64 {
    const FNV_OFFSET: u64 = 0xcbf29ce484222325;
    const FNV_PRIME: u64 = 0x100000001b3;

    let mut hash = FNV_OFFSET;
    for elem in elements {
        if elem.stream == 0xFF {
            break;
        }
        hash ^= elem.stream as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
        hash ^= elem.offset as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
        hash ^= elem.element_type as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
        hash ^= elem.method as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
        hash ^= elem.usage as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
        hash ^= elem.usage_index as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    hash
}

/// Converts a 32-bit D3DFVF bitmask into a sequence of canonical `D3DVertexElement9` elements.
/// Writes into `out_elements` and terminates with `D3DDECL_END`.
/// Returns the number of elements (excluding the terminator), or 0 on error.
pub fn fvf_to_declaration(fvf: u32, out_elements: &mut [D3DVertexElement9]) -> usize {
    if out_elements.is_empty() {
        return 0;
    }

    let mut count = 0;
    let mut offset: u16 = 0;

    let mut push_elem = |e_type: u8, usage: u8, u_idx: u8, off: &mut u16| {
        if count + 1 < out_elements.len() {
            out_elements[count] = D3DVertexElement9 {
                stream: 0,
                offset: *off,
                element_type: e_type,
                method: D3DDECLMETHOD_DEFAULT,
                usage,
                usage_index: u_idx,
            };
            count += 1;
            *off += decl_type_size(e_type) as u16;
        }
    };

    // Position handling
    match fvf & D3DFVF_POSITION_MASK {
        D3DFVF_XYZ => {
            push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, &mut offset);
        }
        D3DFVF_XYZRHW => {
            push_elem(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0, &mut offset);
        }
        D3DFVF_XYZB1 => {
            push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, &mut offset);
            push_elem(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_BLENDWEIGHT, 0, &mut offset);
        }
        D3DFVF_XYZB2 => {
            push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, &mut offset);
            push_elem(D3DDECLTYPE_FLOAT2, D3DDECLUSAGE_BLENDWEIGHT, 0, &mut offset);
        }
        D3DFVF_XYZB3 => {
            push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, &mut offset);
            push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_BLENDWEIGHT, 0, &mut offset);
        }
        D3DFVF_XYZB4 => {
            push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, &mut offset);
            push_elem(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_BLENDWEIGHT, 0, &mut offset);
        }
        D3DFVF_XYZW => {
            push_elem(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0, &mut offset);
        }
        _ => {}
    }

    if (fvf & D3DFVF_NORMAL) != 0 {
        push_elem(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0, &mut offset);
    }
    if (fvf & D3DFVF_PSIZE) != 0 {
        push_elem(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0, &mut offset);
    }
    if (fvf & D3DFVF_DIFFUSE) != 0 {
        push_elem(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, &mut offset);
    }
    if (fvf & D3DFVF_SPECULAR) != 0 {
        push_elem(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1, &mut offset);
    }

    let tex_count = ((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT).min(8);
    for tex_idx in 0..tex_count as u8 {
        // In D3D9 FVF, texture coordinate dimension defaults to 2 floats (UV)
        push_elem(D3DDECLTYPE_FLOAT2, D3DDECLUSAGE_TEXCOORD, tex_idx, &mut offset);
    }

    if count < out_elements.len() {
        out_elements[count] = D3DDECL_END;
    }

    count
}

// C-ABI Exports

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_get_stream_stride(
    elements: *const D3DVertexElement9,
    stream: u32,
) -> u32 {
    if elements.is_null() || stream >= 16 {
        return 0;
    }
    let slice = std::slice::from_raw_parts(elements, 64);
    let mut strides = [0u32; 16];
    compute_stream_strides(slice, &mut strides);
    strides[stream as usize]
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_hash_vertex_declaration(
    elements: *const D3DVertexElement9,
) -> u64 {
    if elements.is_null() {
        return 0;
    }
    let slice = std::slice::from_raw_parts(elements, 64);
    hash_vertex_declaration(slice)
}

#[no_mangle]
pub unsafe extern "C" fn nfs_d3d9_fvf_to_declaration(
    fvf: u32,
    out_elements: *mut D3DVertexElement9,
    max_count: u32,
) -> u32 {
    if out_elements.is_null() || max_count < 2 {
        return 0;
    }
    let slice = std::slice::from_raw_parts_mut(out_elements, max_count as usize);
    fvf_to_declaration(fvf, slice) as u32
}
