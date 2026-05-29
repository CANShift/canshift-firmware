// usb-envelope — Rust port of UsbEnvelope::{findNeedle, findPayloadSlice}.
//
// Originally lived in canshift-firmware/src/hal/usb/usb_envelope.cpp. The C++
// version was extracted from `usb_comm.cpp` (#912) so the host test
// environment could exercise it in isolation without dragging in ArduinoJson /
// NimBLE / LVGL / the storage driver. This crate is the next step:
// pure-logic Rust with a thin C ABI shim so the firmware can opt in via
// `USE_RUST_USB_ENVELOPE=1` and the parity Unity tests
// (`test/test_usb_envelope/`) continue to gate behaviour byte-for-byte.
//
// Why Rust here:
//   - The C++ has a hand-rolled brace-balance + string-state machine. That's
//     where #884 (embedded NUL short-circuit) and #576 (oversized JsonDocument
//     OOM) both lived. A typed Rust port nails down the contract at compile
//     time and removes a class of pointer-arithmetic bugs.
//
// Allocation: zero. Both functions return offsets into the caller's input
// slice — the FFI shim translates them to pointers without moving any bytes.

#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// `find_needle` — length-bounded substring search. Returns the offset of the
// first occurrence of `needle` inside `haystack`, or `None`. Mirrors the C++
// `findNeedle` (used in place of `strstr` so an embedded NUL byte cannot
// short-circuit the search — #884).
//
// A zero-length needle is treated as the error condition and returns `None`,
// not "matches everywhere". This matches the existing C++ contract.
#[must_use]
pub fn find_needle(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || haystack.len() < needle.len() {
        return None;
    }
    let last_start = haystack.len() - needle.len();
    let mut i = 0;
    while i <= last_start {
        if &haystack[i..i + needle.len()] == needle {
            return Some(i);
        }
        i += 1;
    }
    None
}

// `find_payload_slice` — locate the value substring of the `"payload"` key
// inside a top-level JSON object. Returns `Some((start_offset, length))` where
// `start_offset` indexes the opening `{` and `length` covers through the
// matching closing `}` inclusive. Returns `None` when:
//
//   - the input is empty or the `"payload"` key is missing,
//   - the value following the key is not a JSON object,
//   - the input is truncated and the object never closes.
//
// The function is allocation-free and string-aware: `{` / `}` inside JSON
// string values do not affect the depth counter, and `\"` (escaped quote)
// keeps the string-state active so a closing `"` inside the escape is not
// mistaken for the end of the string.
#[must_use]
pub fn find_payload_slice(json_line: &[u8]) -> Option<(usize, usize)> {
    const NEEDLE: &[u8] = b"\"payload\"";
    let needle_off = find_needle(json_line, NEEDLE)?;

    // Skip past `"payload"`, then optional whitespace, then the `:` separator,
    // then optional whitespace, then expect `{`.
    let mut cursor = needle_off + NEEDLE.len();
    cursor = skip_ws(json_line, cursor);
    if cursor >= json_line.len() || json_line[cursor] != b':' {
        return None;
    }
    cursor += 1;
    cursor = skip_ws(json_line, cursor);
    if cursor >= json_line.len() || json_line[cursor] != b'{' {
        return None;
    }

    // Brace-balance walk. Strings are honoured so we don't count `{` / `}`
    // inside JSON string values. Escapes inside strings are skipped verbatim.
    let value_start = cursor;
    let mut depth: i32 = 0;
    let mut in_string = false;
    while cursor < json_line.len() {
        let c = json_line[cursor];
        if in_string {
            if c == b'\\' {
                // Skip the escape byte AND the next byte (whatever it is).
                cursor += 1;
                if cursor < json_line.len() {
                    cursor += 1;
                }
                continue;
            }
            if c == b'"' {
                in_string = false;
            }
            cursor += 1;
            continue;
        }
        match c {
            b'"' => in_string = true,
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    cursor += 1;
                    return Some((value_start, cursor - value_start));
                }
            }
            _ => {}
        }
        cursor += 1;
    }
    None
}

#[inline]
fn skip_ws(buf: &[u8], mut i: usize) -> usize {
    while i < buf.len() {
        match buf[i] {
            b' ' | b'\t' | b'\n' | b'\r' => i += 1,
            _ => break,
        }
    }
    i
}

#[cfg(test)]
mod tests {
    use super::*;

    fn slice(s: &str, range: (usize, usize)) -> &str {
        core::str::from_utf8(&s.as_bytes()[range.0..range.0 + range.1]).unwrap()
    }

    // --- find_needle ---------------------------------------------------------

    #[test]
    fn find_needle_basic_match() {
        assert_eq!(find_needle(b"the quick brown fox", b"quick"), Some(4));
    }

    #[test]
    fn find_needle_first_occurrence_wins() {
        assert_eq!(find_needle(b"abcabc", b"abc"), Some(0));
    }

    #[test]
    fn find_needle_empty_needle_is_error() {
        assert_eq!(find_needle(b"abc", b""), None);
    }

    #[test]
    fn find_needle_haystack_shorter_than_needle() {
        assert_eq!(find_needle(b"ab", b"abc"), None);
    }

    #[test]
    fn find_needle_no_match_in_full_scan() {
        assert_eq!(find_needle(b"abcdef", b"xyz"), None);
    }

    #[test]
    fn find_needle_embedded_nul_does_not_short_circuit() {
        let hay = b"abc\0def_needle";
        assert_eq!(find_needle(hay, b"needle"), Some(8));
    }

    #[test]
    fn find_needle_match_at_end() {
        assert_eq!(find_needle(b"abcdef", b"def"), Some(3));
    }

    // --- find_payload_slice --------------------------------------------------

    #[test]
    fn payload_happy_path() {
        let line = "{\"cmd\":2,\"payload\":{\"a\":1,\"b\":\"x\"}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"a\":1,\"b\":\"x\"}");
    }

    #[test]
    fn payload_nested_object() {
        let line = "{\"cmd\":2,\"payload\":{\"outer\":{\"inner\":42}}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"outer\":{\"inner\":42}}");
    }

    #[test]
    fn payload_braces_inside_string_ignored() {
        let line = "{\"cmd\":2,\"payload\":{\"name\":\"a}b{c\",\"n\":1}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"name\":\"a}b{c\",\"n\":1}");
    }

    #[test]
    fn payload_escaped_quote_inside_string() {
        let line = "{\"cmd\":2,\"payload\":{\"x\":\"a\\\"}\",\"n\":1}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"x\":\"a\\\"}\",\"n\":1}");
    }

    #[test]
    fn payload_whitespace_tolerance() {
        let line = "{\"cmd\":2, \"payload\"  :  \t {\"a\":1}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"a\":1}");
    }

    #[test]
    fn payload_embedded_nul_does_not_short_circuit() {
        // Build a buffer with an embedded NUL, then the real envelope past it.
        // strlen() would stop at the NUL; the length-bounded scan must not.
        let mut buf = Vec::new();
        buf.extend_from_slice(b"abc\0");
        buf.extend_from_slice(b"{\"payload\":{\"a\":1}}");
        let (off, len) = find_payload_slice(&buf).unwrap();
        assert_eq!(&buf[off..off + len], b"{\"a\":1}");
    }

    #[test]
    fn payload_missing_key() {
        let line = b"{\"cmd\":2,\"other\":{\"a\":1}}";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_value_not_object_string() {
        let line = b"{\"cmd\":2,\"payload\":\"not_an_object\"}";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_value_not_object_array() {
        let line = b"{\"cmd\":2,\"payload\":[1,2,3]}";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_missing_colon() {
        let line = b"{\"cmd\":2,\"payload\" {\"a\":1}}";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_unterminated_object() {
        let line = b"{\"cmd\":2,\"payload\":{\"a\":1";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_empty_object() {
        let line = "{\"cmd\":2,\"payload\":{}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{}");
    }

    #[test]
    fn payload_empty_input() {
        assert_eq!(find_payload_slice(b""), None);
    }

    #[test]
    fn payload_key_present_but_truncated_before_colon() {
        let line = b"{\"payload\"";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_key_present_but_truncated_after_colon() {
        let line = b"{\"payload\":";
        assert_eq!(find_payload_slice(line), None);
    }

    #[test]
    fn payload_consecutive_escapes() {
        // `\\` inside a string is a single escaped backslash. The `\\"` that
        // follows is an escaped quote — must NOT close the string. The trailing
        // real `"` closes it.
        let line = "{\"payload\":{\"k\":\"\\\\\\\"end\"}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"k\":\"\\\\\\\"end\"}");
    }
}
