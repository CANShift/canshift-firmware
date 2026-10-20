pub struct Cursor<'a> {
    buf: &'a mut [u8],
    at: usize,
    overflow: bool,
}

impl<'a> Cursor<'a> {
    pub fn new(buf: &'a mut [u8]) -> Self {
        Self {
            buf,
            at: 0,
            overflow: false,
        }
    }

    pub fn put(&mut self, bytes: &[u8]) {
        if self.overflow {
            return;
        }
        let end = self.at + bytes.len();
        let Some(dst) = self.buf.get_mut(self.at..end) else {
            self.overflow = true;
            return;
        };
        dst.copy_from_slice(bytes);
        self.at = end;
    }

    pub fn put_u8(&mut self, value: u8) {
        self.put(&[value]);
    }

    pub fn put_u16(&mut self, value: u16) {
        self.put(&value.to_le_bytes());
    }

    pub fn put_u32(&mut self, value: u32) {
        self.put(&value.to_le_bytes());
    }

    pub fn put_u64(&mut self, value: u64) {
        self.put(&value.to_le_bytes());
    }

    pub fn put_f32(&mut self, value: f32) {
        self.put(&value.to_le_bytes());
    }

    pub fn finish(self) -> Option<usize> {
        if self.overflow {
            return None;
        }
        Some(self.at)
    }
}
