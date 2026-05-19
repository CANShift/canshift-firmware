//! Parity tests for the Rust OTA HMAC verifier — mirror the Unity suite at
//! `canshift-firmware/test/test_ota_hmac/test_main.cpp`. Test names match
//! their C++ counterpart so the cross-check is auditable.
//!
//! Uses a stub HMAC backend identical to the C++ tests (XOR fingerprint
//! seeded by the secret) so we exercise the framing/streaming layer in
//! isolation — RustCrypto's hmac/sha2 correctness is covered by their own
//! upstream suites.

use ota_hmac::{
    constant_time_memcmp, HmacBackend, OtaHmacVerifier, Sink, HMAC_LEN, MAX_SECRET_LEN,
};

// ---------------------------------------------------------------------------
// Stub backend — XOR-fingerprint, byte-for-byte equivalent to the C++ stub.
// ---------------------------------------------------------------------------

#[derive(Default)]
struct StubBackend {
    state: [u8; HMAC_LEN],
    total_bytes: usize,
    updates: Vec<u8>,
    started: bool,
}

impl HmacBackend for StubBackend {
    fn init(&mut self, secret: &[u8]) -> Result<(), ()> {
        self.state = [0; HMAC_LEN];
        self.total_bytes = 0;
        self.updates.clear();
        for (i, b) in secret.iter().enumerate() {
            self.state[i % HMAC_LEN] ^= b;
        }
        self.started = true;
        Ok(())
    }

    fn update(&mut self, data: &[u8]) -> Result<(), ()> {
        if !self.started {
            return Err(());
        }
        for (i, b) in data.iter().enumerate() {
            let pos = self.total_bytes + i;
            self.state[pos % HMAC_LEN] ^= b;
            self.updates.push(*b);
        }
        self.total_bytes += data.len();
        Ok(())
    }

    fn finalize(&mut self, out: &mut [u8; HMAC_LEN]) -> Result<(), ()> {
        *out = self.state;
        self.started = false;
        Ok(())
    }
}

// Same algorithm as StubBackend, computed standalone for crafting valid
// trailers in tests.
fn compute_stub_hmac(secret: &[u8], body: &[u8]) -> [u8; HMAC_LEN] {
    let mut out = [0u8; HMAC_LEN];
    for (i, b) in secret.iter().enumerate() {
        out[i % HMAC_LEN] ^= b;
    }
    for (i, b) in body.iter().enumerate() {
        out[i % HMAC_LEN] ^= b;
    }
    out
}

// Recording sink — captures every byte emitted.
#[derive(Default, Clone)]
struct Recorder {
    body: Vec<u8>,
}

impl Sink for &mut Recorder {
    fn write(&mut self, data: &[u8]) -> Result<(), ()> {
        self.body.extend_from_slice(data);
        Ok(())
    }
}

fn make_upload(secret: &[u8], body: &[u8]) -> Vec<u8> {
    let trailer = compute_stub_hmac(secret, body);
    let mut upload = Vec::with_capacity(body.len() + HMAC_LEN);
    upload.extend_from_slice(body);
    upload.extend_from_slice(&trailer);
    upload
}

// ---------------------------------------------------------------------------
// constant_time_memcmp
// ---------------------------------------------------------------------------

#[test]
fn constant_time_memcmp_equal_returns_zero() {
    let a = [0xAAu8; 32];
    let b = [0xAAu8; 32];
    assert_eq!(constant_time_memcmp(&a, &b), 0);
}

#[test]
fn constant_time_memcmp_different_last_byte_returns_nonzero() {
    let a = [0u8; 32];
    let mut b = [0u8; 32];
    b[31] = 1;
    assert_ne!(constant_time_memcmp(&a, &b), 0);
}

#[test]
fn constant_time_memcmp_different_first_byte_returns_nonzero() {
    let mut a = [0u8; 32];
    let b = [0u8; 32];
    a[0] = 1;
    assert_ne!(constant_time_memcmp(&a, &b), 0);
}

// ---------------------------------------------------------------------------
// Framing / streaming
// ---------------------------------------------------------------------------

#[test]
fn valid_upload_sink_receives_only_body_one_chunk() {
    let secret = b"secret-key-12345";
    let body: Vec<u8> = (0..100u8).collect();
    let upload = make_upload(secret, &body);

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&upload));
    assert!(v.finish());

    assert_eq!(rec.body, body);
}

#[test]
fn chunking_byte_by_byte_equivalent_to_monolithic() {
    let secret = b"key";
    let body: Vec<u8> = (0..200u8).collect();
    let upload = make_upload(secret, &body);

    let mut rec_mono = Recorder::default();
    {
        let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec_mono).unwrap();
        assert!(v.begin());
        assert!(v.feed(&upload));
        assert!(v.finish());
    }

    let mut rec_byte = Recorder::default();
    {
        let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec_byte).unwrap();
        assert!(v.begin());
        for b in &upload {
            assert!(v.feed(&[*b]));
        }
        assert!(v.finish());
    }

    assert_eq!(rec_mono.body, rec_byte.body);
}

#[test]
fn chunking_several_sizes_all_equivalent() {
    let secret = b"another-key";
    let body: Vec<u8> = (0..300u16).map(|i| (i as u8).wrapping_mul(7)).collect();
    let upload = make_upload(secret, &body);

    let chunk_sizes = [1, 7, 13, 31, 32, 33, 64, 100];
    let mut canonical: Option<Vec<u8>> = None;

    for &cs in &chunk_sizes {
        let mut rec = Recorder::default();
        let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec).unwrap();
        assert!(v.begin());
        for chunk in upload.chunks(cs) {
            assert!(v.feed(chunk));
        }
        assert!(v.finish(), "chunk size {cs} failed to verify");

        match canonical {
            None => canonical = Some(rec.body),
            Some(ref base) => assert_eq!(*base, rec.body, "chunk size {cs} body diverged"),
        }
    }
}

#[test]
fn upload_with_empty_body_is_accepted() {
    let secret = b"k";
    let trailer = compute_stub_hmac(secret, &[]);

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&trailer));
    assert!(v.finish());
    assert!(rec.body.is_empty());
}

#[test]
fn upload_shorter_than_trailer_is_rejected() {
    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(StubBackend::default(), b"k", &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&[0u8; 31]));
    assert!(!v.finish());
}

#[test]
fn empty_upload_is_rejected() {
    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(StubBackend::default(), b"k", &mut rec).unwrap();
    assert!(v.begin());
    assert!(!v.finish());
}

#[test]
fn corrupted_trailer_last_byte_is_rejected() {
    let secret = b"k";
    let body = b"hello".to_vec();
    let mut upload = make_upload(secret, &body);
    let last = upload.len() - 1;
    upload[last] ^= 0xFF;

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&upload));
    assert!(!v.finish());
}

#[test]
fn corrupted_body_byte_is_rejected() {
    let secret = b"k";
    let body = b"hello world body bytes".to_vec();
    let mut upload = make_upload(secret, &body);
    upload[3] ^= 0xFF; // flip a body byte BEFORE the trailer

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&upload));
    assert!(!v.finish());
}

// ---------------------------------------------------------------------------
// Sink-failure path
// ---------------------------------------------------------------------------

struct FailingSink {
    fail_after: usize,
    written: usize,
}

impl Sink for FailingSink {
    fn write(&mut self, data: &[u8]) -> Result<(), ()> {
        if self.written + data.len() > self.fail_after {
            return Err(());
        }
        self.written += data.len();
        Ok(())
    }
}

#[test]
fn sink_failure_aborts_feed() {
    let secret = b"k";
    let body: Vec<u8> = (0..100u8).collect();
    let upload = make_upload(secret, &body);

    let sink = FailingSink {
        fail_after: 10,
        written: 0,
    };
    let mut v = OtaHmacVerifier::new(StubBackend::default(), secret, sink).unwrap();
    assert!(v.begin());
    let ok = v.feed(&upload);
    // Sink rejected mid-stream → feed returns false; further finish must
    // also report failure (verifier becomes unusable).
    assert!(!ok);
    assert!(!v.finish());
}

// ---------------------------------------------------------------------------
// Constructor bounds
// ---------------------------------------------------------------------------

#[test]
fn secret_longer_than_max_is_rejected_at_construction() {
    let too_long = vec![0u8; MAX_SECRET_LEN + 1];
    let mut rec = Recorder::default();
    let err = OtaHmacVerifier::new(StubBackend::default(), &too_long, &mut rec);
    assert!(err.is_err());
}
