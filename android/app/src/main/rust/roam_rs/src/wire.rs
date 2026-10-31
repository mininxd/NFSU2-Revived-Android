//! Binary wire encoding and decoding primitives with bounds safety.

#[inline]
pub fn fbits(f: f32) -> u32 {
    f.to_bits()
}

#[inline]
pub fn bitsf(u: u32) -> f32 {
    f32::from_bits(u)
}

pub struct PacketWriter<'a> {
    buf: &'a mut [u8],
    offset: usize,
    error: bool,
}

impl<'a> PacketWriter<'a> {
    pub fn new(buf: &'a mut [u8]) -> Self {
        Self {
            buf,
            offset: 0,
            error: false,
        }
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.offset
    }

    #[inline]
    pub fn has_error(&self) -> bool {
        self.error
    }

    #[inline]
    pub fn put_u8(&mut self, val: u8) {
        if self.offset < self.buf.len() && !self.error {
            self.buf[self.offset] = val;
            self.offset += 1;
        } else {
            self.error = true;
        }
    }

    #[inline]
    pub fn put_u16(&mut self, val: u16) {
        if self.offset + 2 <= self.buf.len() && !self.error {
            let bytes = val.to_le_bytes();
            self.buf[self.offset..self.offset + 2].copy_from_slice(&bytes);
            self.offset += 2;
        } else {
            self.error = true;
        }
    }

    #[inline]
    pub fn put_u32(&mut self, val: u32) {
        if self.offset + 4 <= self.buf.len() && !self.error {
            let bytes = val.to_le_bytes();
            self.buf[self.offset..self.offset + 4].copy_from_slice(&bytes);
            self.offset += 4;
        } else {
            self.error = true;
        }
    }

    #[inline]
    pub fn put_f32(&mut self, val: f32) {
        self.put_u32(val.to_bits());
    }

    #[inline]
    pub fn put_bytes(&mut self, src: &[u8]) {
        if self.offset + src.len() <= self.buf.len() && !self.error {
            self.buf[self.offset..self.offset + src.len()].copy_from_slice(src);
            self.offset += src.len();
        } else {
            self.error = true;
        }
    }

    #[inline]
    pub fn patch_u16(&mut self, offset: usize, val: u16) {
        if offset + 2 <= self.buf.len() {
            let bytes = val.to_le_bytes();
            self.buf[offset..offset + 2].copy_from_slice(&bytes);
        }
    }
}

pub struct PacketReader<'a> {
    buf: &'a [u8],
    offset: usize,
    error: bool,
}

impl<'a> PacketReader<'a> {
    pub fn new(buf: &'a [u8]) -> Self {
        Self {
            buf,
            offset: 0,
            error: false,
        }
    }

    #[inline]
    pub fn remaining(&self) -> usize {
        self.buf.len().saturating_sub(self.offset)
    }

    #[inline]
    pub fn has_error(&self) -> bool {
        self.error
    }

    #[inline]
    pub fn get_u8(&mut self) -> u8 {
        if self.offset < self.buf.len() && !self.error {
            let b = self.buf[self.offset];
            self.offset += 1;
            b
        } else {
            self.error = true;
            0
        }
    }

    #[inline]
    pub fn get_u16(&mut self) -> u16 {
        if self.offset + 2 <= self.buf.len() && !self.error {
            let val = u16::from_le_bytes([self.buf[self.offset], self.buf[self.offset + 1]]);
            self.offset += 2;
            val
        } else {
            self.error = true;
            0
        }
    }

    #[inline]
    pub fn get_u32(&mut self) -> u32 {
        if self.offset + 4 <= self.buf.len() && !self.error {
            let val = u32::from_le_bytes([
                self.buf[self.offset],
                self.buf[self.offset + 1],
                self.buf[self.offset + 2],
                self.buf[self.offset + 3],
            ]);
            self.offset += 4;
            val
        } else {
            self.error = true;
            0
        }
    }

    #[inline]
    pub fn get_f32(&mut self) -> f32 {
        f32::from_bits(self.get_u32())
    }

    #[inline]
    pub fn position(&self) -> usize {
        self.offset
    }

    #[inline]
    pub fn skip(&mut self, n: usize) {
        if self.offset + n <= self.buf.len() && !self.error {
            self.offset += n;
        } else {
            self.error = true;
            self.offset = self.buf.len();
        }
    }

    #[inline]
    pub fn get_slice(&mut self, n: usize) -> &'a [u8] {
        if self.offset + n <= self.buf.len() && !self.error {
            let slice = &self.buf[self.offset..self.offset + n];
            self.offset += n;
            slice
        } else {
            self.error = true;
            &[]
        }
    }

    #[inline]
    pub fn get_bytes<'b>(&mut self, dst: &mut [u8]) {
        if self.offset + dst.len() <= self.buf.len() && !self.error {
            dst.copy_from_slice(&self.buf[self.offset..self.offset + dst.len()]);
            self.offset += dst.len();
        } else {
            self.error = true;
            dst.fill(0);
        }
    }
}
