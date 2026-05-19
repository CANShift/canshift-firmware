//! Smoke test against the real RustCrypto HMAC-SHA256 backend. The Unity
//! suite only exercises the framing layer via a stub; this file makes sure
//! the production backend yields a verifier whose output matches a
//! known-good HMAC vector. If the firmware later swaps mbedTLS for any
//! other HMAC-SHA256 implementation, this test pins the byte-level
//! equivalence.

use hmac::Mac;
use ota_hmac::{OtaHmacVerifier, RustCryptoBackend, Sink};

fn hmac_sha256_oneshot(secret: &[u8], body: &[u8]) -> [u8; 32] {
    let mut m = hmac::Hmac::<sha2::Sha256>::new_from_slice(secret).unwrap();
    m.update(body);
    m.finalize().into_bytes().into()
}

#[derive(Default)]
struct Recorder {
    body: Vec<u8>,
}
impl Sink for &mut Recorder {
    fn write(&mut self, data: &[u8]) -> Result<(), ()> {
        self.body.extend_from_slice(data);
        Ok(())
    }
}

#[test]
fn production_backend_verifies_real_hmac_sha256_trailer() {
    let secret = b"shared-OTA-secret-32-bytes-long!";
    let body: Vec<u8> = (0..1024u16).map(|i| (i as u8).wrapping_mul(11)).collect();
    let trailer = hmac_sha256_oneshot(secret, &body);

    let mut upload = Vec::with_capacity(body.len() + 32);
    upload.extend_from_slice(&body);
    upload.extend_from_slice(&trailer);

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(RustCryptoBackend::new(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&upload));
    assert!(v.finish(), "valid HMAC trailer must verify");
    assert_eq!(rec.body, body, "sink must receive body only, no trailer");
}

#[test]
fn production_backend_rejects_tampered_trailer() {
    let secret = b"shared-OTA-secret-32-bytes-long!";
    let body: Vec<u8> = (0..512u16).map(|i| i as u8).collect();
    let mut trailer = hmac_sha256_oneshot(secret, &body);
    trailer[0] ^= 0x55;

    let mut upload = Vec::with_capacity(body.len() + 32);
    upload.extend_from_slice(&body);
    upload.extend_from_slice(&trailer);

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(RustCryptoBackend::new(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&upload));
    assert!(!v.finish(), "tampered trailer must NOT verify");
}

#[test]
fn production_backend_rejects_tampered_body() {
    let secret = b"shared-OTA-secret-32-bytes-long!";
    let body: Vec<u8> = (0..512u16).map(|i| i as u8).collect();
    let trailer = hmac_sha256_oneshot(secret, &body);

    let mut upload = Vec::with_capacity(body.len() + 32);
    upload.extend_from_slice(&body);
    upload.extend_from_slice(&trailer);
    upload[100] ^= 0xFF; // flip a body byte

    let mut rec = Recorder::default();
    let mut v = OtaHmacVerifier::new(RustCryptoBackend::new(), secret, &mut rec).unwrap();
    assert!(v.begin());
    assert!(v.feed(&upload));
    assert!(!v.finish(), "tampered body must NOT verify");
}
