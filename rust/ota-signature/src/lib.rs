#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

use ed25519_dalek::{Signature, StreamVerifier, VerifyingKey};

include!(concat!(env!("OUT_DIR"), "/release_key.rs"));

pub const OTA_SIGNATURE_BYTES: usize = 64;
pub const OTA_SIGNATURE_STATE_BYTES: usize = 576;
pub const OTA_SIGNATURE_STATE_ALIGN: usize = 16;

pub struct OtaSignature {
    verifier: StreamVerifier,
}

impl OtaSignature {
    #[must_use]
    pub fn begin(public_key: &[u8; 32], signature: &[u8; OTA_SIGNATURE_BYTES]) -> Option<Self> {
        let key = VerifyingKey::from_bytes(public_key).ok()?;
        let verifier = key.verify_stream(&Signature::from_bytes(signature)).ok()?;
        Some(Self { verifier })
    }

    #[must_use]
    pub fn begin_release(signature: &[u8; OTA_SIGNATURE_BYTES]) -> Option<Self> {
        Self::begin(&RELEASE_PUBLIC_KEY, signature)
    }

    pub fn update(&mut self, chunk: &[u8]) {
        self.verifier.update(chunk);
    }

    #[must_use]
    pub fn finish(self) -> bool {
        self.verifier.finalize_and_verify().is_ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const RFC8032_KEY: [u8; 32] = [
        0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e,
        0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4,
        0x66, 0x0c,
    ];
    const RFC8032_MESSAGE: [u8; 1] = [0x72];
    const RFC8032_SIGNATURE: [u8; 64] = [
        0x92, 0xa0, 0x09, 0xa9, 0xf0, 0xd4, 0xca, 0xb8, 0x72, 0x0e, 0x82, 0x0b, 0x5f, 0x64, 0x25,
        0x40, 0xa2, 0xb2, 0x7b, 0x54, 0x16, 0x50, 0x3f, 0x8f, 0xb3, 0x76, 0x22, 0x23, 0xeb, 0xdb,
        0x69, 0xda, 0x08, 0x5a, 0xc1, 0xe4, 0x3e, 0x15, 0x99, 0x6e, 0x45, 0x8f, 0x36, 0x13, 0xd0,
        0xf1, 0x1d, 0x8c, 0x38, 0x7b, 0x2e, 0xae, 0xb4, 0x30, 0x2a, 0xee, 0xb0, 0x0d, 0x29, 0x16,
        0x12, 0xbb, 0x0c, 0x00,
    ];

    fn verify(key: &[u8; 32], signature: &[u8; 64], chunks: &[&[u8]]) -> bool {
        let Some(mut check) = OtaSignature::begin(key, signature) else {
            return false;
        };
        for chunk in chunks {
            check.update(chunk);
        }
        check.finish()
    }

    #[test]
    fn the_rfc8032_vector_verifies() {
        assert!(verify(
            &RFC8032_KEY,
            &RFC8032_SIGNATURE,
            &[&RFC8032_MESSAGE]
        ));
    }

    #[test]
    fn a_tampered_image_is_refused() {
        assert!(!verify(&RFC8032_KEY, &RFC8032_SIGNATURE, &[&[0x73]]));
        assert!(!verify(
            &RFC8032_KEY,
            &RFC8032_SIGNATURE,
            &[&RFC8032_MESSAGE, &[0x00]]
        ));
    }

    #[test]
    fn a_tampered_signature_is_refused() {
        let mut signature = RFC8032_SIGNATURE;
        signature[0] ^= 0x01;
        assert!(!verify(&RFC8032_KEY, &signature, &[&RFC8032_MESSAGE]));
    }

    #[test]
    fn a_non_canonical_scalar_is_refused_at_begin() {
        let mut signature = RFC8032_SIGNATURE;
        signature[63] = 0xff;
        assert!(OtaSignature::begin(&RFC8032_KEY, &signature).is_none());
    }

    #[test]
    fn chunking_does_not_change_the_verdict() {
        let message: Vec<u8> = (0..=255u8).cycle().take(5000).collect();
        let whole = OtaSignature::begin(&RFC8032_KEY, &RFC8032_SIGNATURE);
        let mut split = OtaSignature::begin(&RFC8032_KEY, &RFC8032_SIGNATURE).unwrap();
        let mut joined = whole.unwrap();
        joined.update(&message);
        for chunk in message.chunks(997) {
            split.update(chunk);
        }
        assert_eq!(joined.finish(), split.finish());
    }

    #[test]
    fn the_release_key_is_embedded_from_the_repository_pem() {
        assert_eq!(
            RELEASE_PUBLIC_KEY[..4],
            [0xe9, 0x2a, 0xba, 0xde],
            "keys/firmware-signing-public.pem changed: update this test with the new key"
        );
        assert!(VerifyingKey::from_bytes(&RELEASE_PUBLIC_KEY).is_ok());
    }

    #[test]
    fn a_signature_from_another_key_is_refused_by_the_release_key() {
        assert!(!verify(
            &RELEASE_PUBLIC_KEY,
            &RFC8032_SIGNATURE,
            &[&RFC8032_MESSAGE]
        ));
    }
}
