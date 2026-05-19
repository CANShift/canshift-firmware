//! C ABI bridge for the OTA HMAC verifier.
//!
//! Exposes a minimal set of `extern "C"` entry points so C++ callers
//! (`canshift-firmware/src/hal/wifi/ota_hmac_bridge.cpp`, Phase 3) can use
//! the Rust verifier without touching `unsafe` themselves.
//!
//! Memory ownership:
//!   - All state lives in a single C-opaque `OtaHmacRs` struct.
//!   - The struct is allocated on the C++ stack (caller passes a pointer to
//!     a zero-initialised buffer of `ota_hmac_rs_sizeof()` bytes) — keeps
//!     the Rust side completely no-`alloc` even on `staticlib` builds.
//!   - Slices on the C side are passed as `(ptr, len)` pairs.
//!
//! Return-value convention:
//!   - 0 on success, non-zero on failure. Matches the C++ original's
//!     `bool` semantics (which Phase 1 already mirrors).
//!
//! Why not `bindgen`-generated headers? The surface is 6 functions of
//! primitive types; a hand-written `ota_hmac_rs.h` is 30 lines, versioned,
//! and avoids dragging `libclang` + a `build.rs` into the firmware build.

#![allow(unsafe_code)] // FFI shim — the whole point is the C boundary

use core::slice;

use crate::{HmacBackend, OtaHmacVerifier, RustCryptoBackend, Sink, HMAC_LEN, MAX_SECRET_LEN};

// ---------------------------------------------------------------------------
// Sink shim — bridges `extern "C"` callbacks to the Rust `Sink` trait.
// ---------------------------------------------------------------------------

/// C-callable sink callback. Receives a pointer + length and an opaque
/// user pointer. Returns non-zero on failure (matches C++ `OtaSinkFn`).
pub type SinkCallback =
    Option<unsafe extern "C" fn(data: *const u8, len: usize, user: *mut core::ffi::c_void) -> i32>;

struct CSink {
    callback: SinkCallback,
    user: *mut core::ffi::c_void,
}

impl Sink for CSink {
    fn write(&mut self, data: &[u8]) -> Result<(), ()> {
        let cb = self.callback.ok_or(())?;
        // SAFETY: the caller guarantees `cb` is a valid C function for the
        // verifier's lifetime, `user` is the opaque pointer it expects, and
        // `data` lives as long as this call.
        let rc = unsafe { cb(data.as_ptr(), data.len(), self.user) };
        if rc == 0 {
            Ok(())
        } else {
            Err(())
        }
    }
}

// ---------------------------------------------------------------------------
// Opaque verifier wrapper. Sized + aligned so the C++ caller can drop one
// on the stack via a fixed-size byte buffer; Phase 3's bridge cpp uses
// `alignas(alignof(max_align_t)) uint8_t buf[ota_hmac_rs_sizeof()]`.
// ---------------------------------------------------------------------------

/// FFI opaque struct. NOT to be inspected from C — callers treat it as an
/// opaque blob. The layout is intentionally `#[repr(C)]` so its size +
/// alignment are stable across Rust toolchain versions.
#[repr(C)]
pub struct OtaHmacRs {
    inner: Option<OtaHmacVerifier<RustCryptoBackend, CSink>>,
}

/// Returns the size (bytes) of `OtaHmacRs` so the C side can reserve
/// storage. Const-eval friendly — callers can compile-time-assert.
#[no_mangle]
pub extern "C" fn ota_hmac_rs_sizeof() -> usize {
    core::mem::size_of::<OtaHmacRs>()
}

/// Returns the alignment requirement of `OtaHmacRs` in bytes.
#[no_mangle]
pub extern "C" fn ota_hmac_rs_alignof() -> usize {
    core::mem::align_of::<OtaHmacRs>()
}

/// Initialise an `OtaHmacRs` at `slot` (caller-owned storage). The slot
/// must point to at least `ota_hmac_rs_sizeof()` bytes aligned to
/// `ota_hmac_rs_alignof()` and **must be zeroed** before this call.
///
/// Returns 0 on success, non-zero if:
///   - `secret_len > MAX_SECRET_LEN` (64)
///   - `sink_cb` is null
///   - the HMAC backend rejected initialisation
///
/// On failure the slot is left in a defined drop-safe state.
///
/// # Safety
/// - `slot` must be writable for `ota_hmac_rs_sizeof()` bytes
/// - `secret` must point to `secret_len` readable bytes (or be null when
///   `secret_len == 0`)
/// - `sink_cb` / `sink_user` are stored and called on every `feed()` —
///   they must remain valid until `ota_hmac_rs_destroy` runs
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_init(
    slot: *mut OtaHmacRs,
    secret: *const u8,
    secret_len: usize,
    sink_cb: SinkCallback,
    sink_user: *mut core::ffi::c_void,
) -> i32 {
    if slot.is_null() || sink_cb.is_none() {
        return 1;
    }
    if secret_len > MAX_SECRET_LEN {
        return 2;
    }
    let secret_slice = if secret_len == 0 {
        &[][..]
    } else if secret.is_null() {
        return 3;
    } else {
        unsafe { slice::from_raw_parts(secret, secret_len) }
    };

    let sink = CSink {
        callback: sink_cb,
        user: sink_user,
    };
    let verifier = match OtaHmacVerifier::new(RustCryptoBackend::new(), secret_slice, sink) {
        Ok(v) => v,
        Err(_) => return 4,
    };

    // Write through a raw pointer so we don't drop whatever uninit garbage
    // was in `slot` before. `OtaHmacRs::inner` is `Option<…>` so the
    // initial `None` state is valid for any byte pattern.
    unsafe {
        core::ptr::write(
            slot,
            OtaHmacRs {
                inner: Some(verifier),
            },
        )
    };
    0
}

/// Call once before any `feed()`. Returns 0 on success.
///
/// # Safety
/// `slot` must point to an `OtaHmacRs` previously initialised by
/// `ota_hmac_rs_init` and not yet destroyed.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_begin(slot: *mut OtaHmacRs) -> i32 {
    if slot.is_null() {
        return 1;
    }
    let v = unsafe { &mut *slot };
    let Some(inner) = v.inner.as_mut() else {
        return 1;
    };
    if inner.begin() {
        0
    } else {
        1
    }
}

/// Stream a chunk of upload bytes.
///
/// # Safety
/// `slot` must be initialised; `data` readable for `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_feed(
    slot: *mut OtaHmacRs,
    data: *const u8,
    len: usize,
) -> i32 {
    if slot.is_null() {
        return 1;
    }
    let buf = if len == 0 {
        &[][..]
    } else if data.is_null() {
        return 1;
    } else {
        unsafe { slice::from_raw_parts(data, len) }
    };
    let v = unsafe { &mut *slot };
    let Some(inner) = v.inner.as_mut() else {
        return 1;
    };
    if inner.feed(buf) {
        0
    } else {
        1
    }
}

/// Finalise. Returns 0 iff the trailing 32 bytes matched the computed HMAC.
///
/// # Safety
/// `slot` must be initialised.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_finish(slot: *mut OtaHmacRs) -> i32 {
    if slot.is_null() {
        return 1;
    }
    let v = unsafe { &mut *slot };
    let Some(inner) = v.inner.as_mut() else {
        return 1;
    };
    if inner.finish() {
        0
    } else {
        1
    }
}

/// Total bytes accepted via `feed()` (body + trailer). Returns 0 when the
/// slot is null or not initialised.
///
/// # Safety
/// `slot` must be a valid `OtaHmacRs` pointer or null.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_total_bytes(slot: *const OtaHmacRs) -> usize {
    if slot.is_null() {
        return 0;
    }
    let v = unsafe { &*slot };
    v.inner.as_ref().map_or(0, OtaHmacVerifier::total_bytes)
}

/// Release the verifier. After this call `slot` is overwritten with the
/// initial empty state and must be re-initialised before further use.
///
/// # Safety
/// `slot` must point to an `OtaHmacRs` previously initialised by
/// `ota_hmac_rs_init`.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_destroy(slot: *mut OtaHmacRs) {
    if slot.is_null() {
        return;
    }
    // Drop the verifier in place by replacing with an empty wrapper.
    unsafe { core::ptr::write(slot, OtaHmacRs { inner: None }) };
}

/// Constant-time byte-buffer compare. Returns 0 iff `a` and `b` are equal
/// over `len` bytes. Exposed for callers that need the primitive
/// independently of the verifier (mirrors `OtaHmac::constantTimeMemcmp`).
///
/// # Safety
/// `a` and `b` must each be readable for `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_const_memcmp(a: *const u8, b: *const u8, len: usize) -> i32 {
    if len == 0 {
        return 0;
    }
    if a.is_null() || b.is_null() {
        return 1;
    }
    let aa = unsafe { slice::from_raw_parts(a, len) };
    let bb = unsafe { slice::from_raw_parts(b, len) };
    crate::constant_time_memcmp(aa, bb) as i32
}

// ---------------------------------------------------------------------------
// Raw HMAC primitive — separate from the streaming verifier above.
//
// The C++ side already owns the rolling-window framing layer in
// `ota_hmac.cpp::OtaHmacVerifier`; that layer needs an `HmacBackend` that
// answers `init(secret) -> ctx`, `update(ctx, data)`, `finalize(ctx, out)`
// with no trailer logic. The streaming Rust verifier (`OtaHmacRs`) wraps
// the full framing inside, so it would re-window the bytes — wrong here.
//
// This second ABI surface exposes the underlying HMAC-SHA256 primitive
// directly so the C++ HmacBackend bridge can swap mbedTLS → RustCrypto
// without changing the framing layer.
// ---------------------------------------------------------------------------

/// Opaque raw HMAC state — stores a `RustCryptoBackend` internally.
#[repr(C)]
pub struct OtaHmacRsRaw {
    inner: Option<RustCryptoBackend>,
}

#[no_mangle]
pub extern "C" fn ota_hmac_rs_raw_sizeof() -> usize {
    core::mem::size_of::<OtaHmacRsRaw>()
}

#[no_mangle]
pub extern "C" fn ota_hmac_rs_raw_alignof() -> usize {
    core::mem::align_of::<OtaHmacRsRaw>()
}

/// Initialise a raw HMAC at `slot` (zeroed caller storage). Returns 0 on
/// success.
///
/// # Safety
/// `slot` must point to zeroed storage of `ota_hmac_rs_raw_sizeof()` bytes
/// with `ota_hmac_rs_raw_alignof()` alignment. `secret` must be readable
/// for `secret_len` bytes (or null when `secret_len == 0`).
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_raw_init(
    slot: *mut OtaHmacRsRaw,
    secret: *const u8,
    secret_len: usize,
) -> i32 {
    if slot.is_null() {
        return 1;
    }
    let secret_slice = if secret_len == 0 {
        &[][..]
    } else if secret.is_null() {
        return 2;
    } else {
        unsafe { slice::from_raw_parts(secret, secret_len) }
    };
    let mut backend = RustCryptoBackend::new();
    if backend.init(secret_slice).is_err() {
        return 3;
    }
    unsafe {
        core::ptr::write(
            slot,
            OtaHmacRsRaw {
                inner: Some(backend),
            },
        )
    };
    0
}

/// Feed bytes into the HMAC computation.
///
/// # Safety
/// `slot` previously initialised; `data` readable for `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_raw_update(
    slot: *mut OtaHmacRsRaw,
    data: *const u8,
    len: usize,
) -> i32 {
    if slot.is_null() {
        return 1;
    }
    let buf = if len == 0 {
        &[][..]
    } else if data.is_null() {
        return 1;
    } else {
        unsafe { slice::from_raw_parts(data, len) }
    };
    let s = unsafe { &mut *slot };
    let Some(backend) = s.inner.as_mut() else {
        return 1;
    };
    if backend.update(buf).is_err() {
        return 1;
    }
    0
}

/// Finalise: write 32 bytes of HMAC into `out`. Returns 0 on success. The
/// slot is left in a destroyed state and must be re-initialised before
/// further use.
///
/// # Safety
/// `slot` previously initialised; `out` writable for 32 bytes.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_raw_finalize(slot: *mut OtaHmacRsRaw, out: *mut u8) -> i32 {
    if slot.is_null() || out.is_null() {
        return 1;
    }
    let s = unsafe { &mut *slot };
    let Some(backend) = s.inner.as_mut() else {
        return 1;
    };
    let mut tmp = [0u8; HMAC_LEN];
    if backend.finalize(&mut tmp).is_err() {
        return 1;
    }
    unsafe { core::ptr::copy_nonoverlapping(tmp.as_ptr(), out, HMAC_LEN) };
    unsafe { core::ptr::write(slot, OtaHmacRsRaw { inner: None }) };
    0
}

/// Release a raw HMAC slot without computing the HMAC. Use this for early
/// abort paths.
///
/// # Safety
/// `slot` previously initialised or zero-filled.
#[no_mangle]
pub unsafe extern "C" fn ota_hmac_rs_raw_destroy(slot: *mut OtaHmacRsRaw) {
    if slot.is_null() {
        return;
    }
    unsafe { core::ptr::write(slot, OtaHmacRsRaw { inner: None }) };
}
