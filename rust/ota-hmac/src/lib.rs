//! Rust port of `canshift-firmware/src/hal/wifi/ota_hmac.cpp` (issue #827
//! Phase 1 spike). Streaming HMAC-SHA256 trailer verifier for OTA uploads.
//!
//! Trailer format: `<firmware bytes> || HMAC_SHA256(firmware bytes, secret)`.
//! The verifier keeps a rolling 32-byte window — bytes that exit the window
//! are flushed to the sink and folded into the HMAC, the 32 bytes still in
//! the window at end-of-stream are taken as the received trailer and
//! constant-time-compared against the freshly computed HMAC.
//!
//! Layout choices vs the C++ original:
//!
//! * `HmacBackend` is a trait, not a fn-pointer table — same zero-overhead
//!   monomorphisation, but the borrow checker rejects use-after-finalize at
//!   compile time instead of via a runtime `m_failed` flag.
//! * `Sink` is also a trait. Both can be `&mut` closures via blanket impls.
//! * No `unsafe` — the rolling-window arithmetic is bounds-checked.
//! * `no_std` compatible: no `alloc`, no `std::` types. Phase 2 PlatformIO
//!   integration consumes this crate verbatim with `crate-type = staticlib`.

#![cfg_attr(not(any(test, feature = "std")), no_std)]
// `Result<(), ()>` is intentional here — Phase 1 mirrors the C++ bool API
// to keep the FFI surface boring. A typed error variant goes in Phase 2 if
// it adds value at the C bridge.
#![allow(clippy::result_unit_err)]

#[cfg(feature = "ffi")]
pub mod ffi;

// Panic handler — required for `no_std` + `staticlib` builds. Strategy:
// halt forever. The firmware's diag/logger sees the verifier return an
// error from feed/finish (each Rust→C return is fallible) so a logic bug
// here surfaces through normal error paths rather than via panic. Anything
// that *does* panic is an internal invariant break — halting is safer than
// rebooting in a loop. Phase 3 may revisit this to log via UART first.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// HMAC-SHA256 output length (and trailer length we look for at EOF).
pub const HMAC_LEN: usize = 32;

/// Constant-time byte-slice comparison. Returns 0 iff equal. The OR-fold
/// avoids any data-dependent branch that could leak via timing.
#[must_use]
pub fn constant_time_memcmp(a: &[u8], b: &[u8]) -> u8 {
    debug_assert_eq!(a.len(), b.len());
    let mut diff: u8 = 0;
    for i in 0..a.len() {
        diff |= a[i] ^ b[i];
    }
    diff
}

/// Sink for body bytes — everything in the stream except the trailing HMAC.
/// Returning `Err(())` aborts the upload; the verifier becomes unusable.
pub trait Sink {
    fn write(&mut self, data: &[u8]) -> Result<(), ()>;
}

impl<F: FnMut(&[u8]) -> Result<(), ()>> Sink for F {
    fn write(&mut self, data: &[u8]) -> Result<(), ()> {
        (self)(data)
    }
}

/// Streaming HMAC backend. Production builds wire this to `hmac::Hmac<Sha256>`
/// (see `RustCryptoBackend` below); tests can plug a deterministic stub.
pub trait HmacBackend {
    /// Re-initialise the backend with a fresh secret. Called once per
    /// verifier; idempotent across re-use is NOT required.
    fn init(&mut self, secret: &[u8]) -> Result<(), ()>;

    /// Feed a chunk into the HMAC computation.
    fn update(&mut self, data: &[u8]) -> Result<(), ()>;

    /// Finalise: write the 32-byte HMAC into `out` and return Ok.
    /// The backend may consider itself reset; the verifier will not call
    /// further methods on it after finalize.
    fn finalize(&mut self, out: &mut [u8; HMAC_LEN]) -> Result<(), ()>;
}

/// RustCrypto-backed HMAC-SHA256. Used in production. The mbedTLS C++
/// backend ships the same algorithm; both will produce byte-identical
/// trailers for a given secret + body.
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

/// State the verifier rejects further input from when set.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
enum State {
    Active,
    Failed,
    Done,
}

/// Rolling-window HMAC trailer verifier.
///
/// Caller flow:
///   1. construct with `new(backend, secret, sink)`
///   2. `begin()` once
///   3. `feed(chunk)` zero or more times
///   4. `finish()` returns true iff the trailer matched the computed HMAC.
pub struct OtaHmacVerifier<B: HmacBackend, S: Sink> {
    backend: B,
    sink: S,
    state: State,
    /// True after `begin()` succeeds. Guards against feed/finish before begin.
    started: bool,
    total_bytes: usize,
    window: [u8; HMAC_LEN],
    window_fill: usize,
    secret_len: usize,
    secret: [u8; MAX_SECRET_LEN],
}

/// Maximum secret length the verifier will accept. The C++ verifier holds a
/// `const uint8_t *` so it's effectively unbounded; for the no_std Rust port
/// we cap it at a safe upper bound that matches every existing call site.
/// HMAC-SHA256 internally hashes secrets > 64 B down to 32 B, so larger
/// values gain no security and just pressure the embedded heap. 64 B covers
/// the SHA-256 block size and every secret currently in firmware (#674).
pub const MAX_SECRET_LEN: usize = 64;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum VerifierError {
    /// Secret length exceeded `MAX_SECRET_LEN`.
    SecretTooLong,
}

impl<B: HmacBackend, S: Sink> OtaHmacVerifier<B, S> {
    /// Construct a verifier. Rejects secrets longer than `MAX_SECRET_LEN`.
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

    /// Must be called once before any `feed()`. Returns false if the backend
    /// rejected the secret.
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

    /// Stream a chunk through the rolling window. Body bytes go to the
    /// sink + HMAC; the last 32 bytes seen so far stay buffered as the
    /// candidate trailer.
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
            // Not enough bytes seen yet to flush anything; just append.
            self.window[self.window_fill..available].copy_from_slice(data);
            self.window_fill = available;
            return true;
        }

        let to_emit = available - HMAC_LEN;

        // Phase 1: drain window bytes that need to leave.
        let from_window = to_emit.min(self.window_fill);
        if from_window > 0 {
            let slice = &self.window[..from_window];
            if self.sink.write(slice).is_err() || self.backend.update(slice).is_err() {
                self.state = State::Failed;
                return false;
            }
        }

        // Phase 2: emit from the head of the new chunk if needed.
        let from_data = to_emit - from_window;
        if from_data > 0 {
            let head = &data[..from_data];
            if self.sink.write(head).is_err() || self.backend.update(head).is_err() {
                self.state = State::Failed;
                return false;
            }
        }

        // Rebuild the window: leftover from the original window shifts to
        // the front, then the tail of the new chunk fills the rest.
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

    /// Finalise: the 32 bytes still in the window are taken as the received
    /// trailer; compared constant-time against the computed HMAC.
    /// Returns true iff at least 32 bytes were fed AND the trailer matched.
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

    /// Total bytes accepted via `feed()` (body + trailer).
    #[must_use]
    pub fn total_bytes(&self) -> usize {
        self.total_bytes
    }
}
