use core::sync::atomic::{AtomicU32, Ordering};

#[repr(C)]
pub struct Ring {
    storage: *mut u8,
    capacity: u32,
    head: AtomicU32,
    tail: AtomicU32,
    dropped: AtomicU32,
}

impl Ring {
    #[allow(clippy::missing_safety_doc)]
    #[must_use]
    pub unsafe fn new(storage: *mut u8, capacity: u32) -> Option<Self> {
        if storage.is_null() || !capacity.is_power_of_two() {
            return None;
        }
        Some(Self {
            storage,
            capacity,
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
            dropped: AtomicU32::new(0),
        })
    }

    #[must_use]
    pub fn used(&self) -> u32 {
        let head = self.head.load(Ordering::Acquire);
        head.wrapping_sub(self.tail.load(Ordering::Acquire))
    }

    #[must_use]
    pub fn dropped(&self) -> u32 {
        self.dropped.load(Ordering::Acquire)
    }

    pub fn push(&self, bytes: &[u8]) -> bool {
        let fits = u32::try_from(bytes.len()).is_ok_and(|len| len <= self.capacity - self.used());
        if !fits {
            let dropped = self.dropped.load(Ordering::Relaxed);
            self.dropped
                .store(dropped.wrapping_add(1), Ordering::Release);
            return false;
        }
        let head = self.head.load(Ordering::Relaxed);
        self.copy_in(head, bytes);
        self.head
            .store(head.wrapping_add(bytes.len() as u32), Ordering::Release);
        true
    }

    pub fn pop_into(&self, dst: &mut [u8]) -> usize {
        let available = self.used() as usize;
        let len = available.min(dst.len());
        let tail = self.tail.load(Ordering::Relaxed);
        self.copy_out(tail, &mut dst[..len]);
        self.tail
            .store(tail.wrapping_add(len as u32), Ordering::Release);
        len
    }

    fn split(&self, position: u32, len: usize) -> (usize, usize) {
        let start = (position & (self.capacity - 1)) as usize;
        let first = len.min(self.capacity as usize - start);
        (start, first)
    }

    fn copy_in(&self, head: u32, bytes: &[u8]) {
        let (start, first) = self.split(head, bytes.len());
        let (front, back) = bytes.split_at(first);
        unsafe {
            core::ptr::copy_nonoverlapping(front.as_ptr(), self.storage.add(start), front.len());
            core::ptr::copy_nonoverlapping(back.as_ptr(), self.storage, back.len());
        }
    }

    fn copy_out(&self, tail: u32, dst: &mut [u8]) {
        let (start, first) = self.split(tail, dst.len());
        let (front, back) = dst.split_at_mut(first);
        unsafe {
            core::ptr::copy_nonoverlapping(
                self.storage.add(start),
                front.as_mut_ptr(),
                front.len(),
            );
            core::ptr::copy_nonoverlapping(self.storage, back.as_mut_ptr(), back.len());
        }
    }
}

#[cfg(test)]
mod tests;
