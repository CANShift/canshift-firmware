#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

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

#[must_use]
pub fn find_payload_slice(json_line: &[u8]) -> Option<(usize, usize)> {
    const NEEDLE: &[u8] = b"\"payload\"";
    let needle_off = find_needle(json_line, NEEDLE)?;
    let colon = expect_byte(json_line, needle_off + NEEDLE.len(), b':')?;
    let value_start = expect_byte(json_line, colon + 1, b'{')?;
    let value_end = object_end(json_line, value_start)?;
    Some((value_start, value_end - value_start))
}

fn expect_byte(buf: &[u8], from: usize, byte: u8) -> Option<usize> {
    let cursor = skip_ws(buf, from);
    (buf.get(cursor) == Some(&byte)).then_some(cursor)
}

fn string_end(buf: &[u8], mut cursor: usize) -> usize {
    while cursor < buf.len() {
        match buf[cursor] {
            b'\\' => cursor += 2,
            b'"' => return cursor + 1,
            _ => cursor += 1,
        }
    }
    buf.len()
}

fn object_end(buf: &[u8], start: usize) -> Option<usize> {
    let mut depth: i32 = 0;
    let mut cursor = start;
    while cursor < buf.len() {
        match buf[cursor] {
            b'"' => {
                cursor = string_end(buf, cursor + 1);
                continue;
            }
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(cursor + 1);
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
        &s[range.0..range.0 + range.1]
    }

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
        let line = "{\"payload\":{\"k\":\"\\\\\\\"end\"}}";
        let (off, len) = find_payload_slice(line.as_bytes()).unwrap();
        assert_eq!(slice(line, (off, len)), "{\"k\":\"\\\\\\\"end\"}");
    }
}
