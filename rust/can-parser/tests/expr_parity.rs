use can_parser::expr::{EvalContext, eval};

const FIXTURES_JSON: &str = include_str!("expr-parity.json");

fn parse_number(s: &str) -> f32 {
    s.trim().parse().unwrap_or(0.0)
}

fn parse_bytes(s: &str) -> Vec<u8> {
    let inner = s.trim().trim_start_matches('[').trim_end_matches(']');
    if inner.trim().is_empty() {
        return vec![];
    }
    inner
        .split(',')
        .map(|p| p.trim().parse().unwrap_or(0))
        .collect()
}

fn extract_field<'a>(blob: &'a str, key: &str) -> &'a str {
    let pat = format!("\"{key}\":");
    let i = blob.find(&pat).expect("key not found");
    let after = &blob[i + pat.len()..];
    let after = after.trim_start();
    if after.starts_with('"') {
        let rest = &after[1..];
        let end = rest.find('"').expect("unterminated string");
        &rest[..end]
    } else if after.starts_with('[') {
        let end = after.find(']').expect("unterminated array") + 1;
        &after[..end]
    } else {
        let end = after
            .find(|c: char| c == ',' || c == '}')
            .expect("number not terminated");
        after[..end].trim()
    }
}

#[test]
fn rust_matches_fixtures() {
    let blob = FIXTURES_JSON;
    let mut start = 0;
    let mut count = 0;
    while let Some(open) = blob[start..].find('{') {
        let open_abs = start + open;
        let close = blob[open_abs..].find('}').expect("unterminated record");
        let record = &blob[open_abs..=open_abs + close];

        let expr_str = extract_field(record, "expr");
        let v_str = extract_field(record, "v");
        let bytes_str = extract_field(record, "bytes");
        let expected_str = extract_field(record, "expected");

        let v = parse_number(v_str);
        let bytes = parse_bytes(bytes_str);
        let expected = parse_number(expected_str);

        let actual = eval(
            expr_str.as_bytes(),
            &EvalContext {
                v,
                bytes: &bytes,
            },
        );
        let tol = 1e-4_f32 * expected.abs().max(1.0);
        assert!(
            (actual - expected).abs() < tol,
            "expr={expr_str} v={v} bytes={bytes:?} expected={expected} actual={actual}"
        );

        start = open_abs + close + 1;
        count += 1;
    }
    assert!(count > 0, "no fixtures parsed");
}
