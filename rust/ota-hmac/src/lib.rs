//! Streaming HMAC-SHA256 trailer verifier for OTA (#827).
//! Trailer = body || HMAC(body, secret). Rolling 32-byte window flushes to
//! sink; bytes still in the window at EOF are the received trailer.

#![cfg_attr(not(any(test, feature = "std")), no_std)]
#![allow(clippy::result_unit_err)]

#[cfg(feature = "ffi")]
pub mod ffi;

// Required for no_std staticlib — reaching here means invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// HMAC-SHA256 output length (and trailer length we look for at EOF).
pub const HMAC_LEN: usize = 32;

/// OR-fold avoids data-dependent branches that could leak via timing.
#[must_use]
pub fn constant_time_memcmp(a: &[u8], b: &[u8]) -> u8 {
    debug_assert_eq!(a.len(), b.len());
    let mut diff: u8 = 0;
    for i in 0..a.len() {
        diff |= a[i] ^ b[i];
    }
    diff
}

/// Receives body bytes (everything except the trailing HMAC).
pub trait Sink {
    fn write(&mut self, data: &[u8]) -> Result<(), ()>;
}

impl<F: FnMut(&[u8]) -> Result<(), ()>> Sink for F {
    fn write(&mut self, data: &[u8]) -> Result<(), ()> {
        (self)(data)
    }
}

pub trait HmacBackend {
    fn init(&mut self, secret: &[u8]) -> Result<(), ()>;
    fn update(&mut self, data: &[u8]) -> Result<(), ()>;
    fn finalize(&mut self, out: &mut [u8; HMAC_LEN]) -> Result<(), ()>;
}

pub struct RustCryptoBackend {
    inner: Option<hmac::Hmac<sha2::Sha256>>,
}

impl RustCryptoBackend {
    #[must_use]
    pub const fn new() -> Self {
        Self { inner: None }
    }
}

impl Default for RustCryptoBackend {
    fn default() -> Self {
        Self::new()
    }
}

impl HmacBackend for RustCryptoBackend {
    fn init(&mut self, secret: &[u8]) -> Result<(), ()> {
        use hmac::Mac;
        // `new_from_slice` accepts secrets of any length, including 0 (HMAC
        // is defined for any key length and pads/hashes accordingly).
        let inner = hmac::Hmac::<sha2::Sha256>::new_from_slice(secret).map_err(|_| ())?;
        self.inner = Some(inner);
        Ok(())
    }

    fn update(&mut self, data: &[u8]) -> Result<(), ()> {
        use hmac::Mac;
        let m = self.inner.as_mut().ok_or(())?;
        m.update(data);
        Ok(())
    }

    fn finalize(&mut self, out: &mut [u8; HMAC_LEN]) -> Result<(), ()> {
        use hmac::Mac;
        let m = self.inner.take().ok_or(())?;
        let result = m.finalize().into_bytes();
        out.copy_from_slice(&result);
        Ok(())
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
enum State {
    Active,
    Failed,
    Done,
}

/// Caller flow: new → begin → feed* → finish (returns true on trailer match).
pub struct OtaHmacVerifier<B: HmacBackend, S: Sink> {
    backend: B,
    sink: S,
    state: State,
    started: bool,
    total_bytes: usize,
    window: [u8; HMAC_LEN],
    window_fill: usize,
    secret_len: usize,
    secret: [u8; MAX_SECRET_LEN],
}

// SHA-256 block size — HMAC hashes secrets > 64 B down to 32 anyway.
pub const MAX_SECRET_LEN: usize = 64;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum VerifierError {
    SecretTooLong,
}

impl<B: HmacBackend, S: Sink> OtaHmacVerifier<B, S> {
    pub fn new(backend: B, secret: &[u8], sink: S) -> Result<Self, VerifierError> {
        if secret.len() > MAX_SECRET_LEN {
            return Err(VerifierError::SecretTooLong);
        }
        let mut secret_buf = [0u8; MAX_SECRET_LEN];
        secret_buf[..secret.len()].copy_from_slice(secret);
        Ok(Self {
            backend,
            sink,
            state: State::Active,
            started: false,
            total_bytes: 0,
            window: [0; HMAC_LEN],
            window_fill: 0,
            secret_len: secret.len(),
            secret: secret_buf,
        })
    }

    /// Returns false if the backend rejected the secret.
    pub fn begin(&mut self) -> bool {
        if self.started || self.state != State::Active {
            self.state = State::Failed;
            return false;
        }
        if self.backend.init(&self.secret[..self.secret_len]).is_err() {
            self.state = State::Failed;
            return false;
        }
        self.started = true;
        true
    }

    /// Body bytes → sink + HMAC; last 32 bytes stay buffered as trailer.
    pub fn feed(&mut self, data: &[u8]) -> bool {
        if self.state != State::Active || !self.started {
            self.state = State::Failed;
            return false;
        }
        if data.is_empty() {
            return true;
        }
        self.total_bytes += data.len();

        let available = self.window_fill + data.len();
        if available <= HMAC_LEN {
            self.window[self.window_fill..available].copy_from_slice(data);
            self.window_fill = available;
            return true;
        }

        let to_emit = available - HMAC_LEN;

        let from_window = to_emit.min(self.window_fill);
        if from_window > 0 {
            let slice = &self.window[..from_window];
            if self.sink.write(slice).is_err() || self.backend.update(slice).is_err() {
                self.state = State::Failed;
                return false;
            }
        }

        let from_data = to_emit - from_window;
        if from_data > 0 {
            let head = &data[..from_data];
            if self.sink.write(head).is_err() || self.backend.update(head).is_err() {
                self.state = State::Failed;
                return false;
            }
        }

        let window_leftover = self.window_fill - from_window;
        if window_leftover > 0 {
            self.window.copy_within(from_window..self.window_fill, 0);
        }
        let from_data_to_window = data.len() - from_data;
        if from_data_to_window > 0 {
            let target = window_leftover..window_leftover + from_data_to_window;
            self.window[target].copy_from_slice(&data[from_data..]);
        }
        self.window_fill = window_leftover + from_data_to_window;
        true
    }

    /// True iff ≥32 bytes were fed AND the window matches the computed HMAC.
    pub fn finish(&mut self) -> bool {
        if self.state != State::Active || !self.started {
            self.state = State::Failed;
            return false;
        }
        if self.window_fill != HMAC_LEN {
            self.state = State::Failed;
            return false;
        }
        let mut computed = [0u8; HMAC_LEN];
        if self.backend.finalize(&mut computed).is_err() {
            self.state = State::Failed;
            return false;
        }
        self.state = State::Done;
        constant_time_memcmp(&computed, &self.window) == 0
    }

    #[must_use]
    pub fn total_bytes(&self) -> usize {
        self.total_bytes
    }
}
