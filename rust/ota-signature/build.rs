use base64::Engine;
use std::{env, fs, path::Path};

const KEY_PATH: &str = "../../keys/firmware-signing-public.pem";
const SPKI_ED25519_PREFIX: [u8; 12] = [
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
];
const KEY_BYTES: usize = 32;

fn main() {
    println!("cargo:rerun-if-changed={KEY_PATH}");
    let pem = fs::read_to_string(KEY_PATH).expect("firmware signing public key is readable");
    let body: String = pem
        .lines()
        .filter(|line| !line.starts_with("-----"))
        .collect();
    let der = base64::engine::general_purpose::STANDARD
        .decode(body.trim())
        .expect("public key PEM body is base64");
    assert!(
        der.len() == SPKI_ED25519_PREFIX.len() + KEY_BYTES && der.starts_with(&SPKI_ED25519_PREFIX),
        "{KEY_PATH} is not an Ed25519 SubjectPublicKeyInfo"
    );
    let key = &der[SPKI_ED25519_PREFIX.len()..];
    let out = Path::new(&env::var("OUT_DIR").expect("cargo sets OUT_DIR")).join("release_key.rs");
    fs::write(
        out,
        format!("pub const RELEASE_PUBLIC_KEY: [u8; {KEY_BYTES}] = {key:?};\n"),
    )
    .expect("OUT_DIR is writable");
}
