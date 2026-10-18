use super::*;

#[test]
fn get_all_returns_newest_first() {
    let mut s = Store::new();

    s.push(1, "A", "first");
    s.push(2, "B", "second");
    s.push(3, "C", "third");

    let mut out = [FwError {
        source: 0,
        code: [0; CODE_LEN],
        message: [0; MESSAGE_LEN],
    }; 4];
    let n = get_all(&s.ring, s.head, s.count, &mut out);
    assert_eq!(n, 3);
    assert_eq!(code_of(&out[0]), "C");
    assert_eq!(code_of(&out[1]), "B");
    assert_eq!(code_of(&out[2]), "A");
}

#[test]
fn get_all_caps_at_out_len() {
    let mut s = Store::new();

    for i in 0..6 {
        let code = format!("C{i}");
        s.push(1, &code, "");
    }

    let mut out = [FwError {
        source: 0,
        code: [0; CODE_LEN],
        message: [0; MESSAGE_LEN],
    }; 2];
    let n = get_all(&s.ring, s.head, s.count, &mut out);
    assert_eq!(n, 2);
    assert_eq!(code_of(&out[0]), "C5");
    assert_eq!(code_of(&out[1]), "C4");
}

#[test]
fn get_all_handles_post_wrap_state() {
    let mut s = Store::new();

    for i in 0..7 {
        let code = format!("C{i}");
        s.push(1, &code, "");
    }
    assert_eq!(s.head, 1);
    assert_eq!(s.count, 6);

    let mut out = [FwError {
        source: 0,
        code: [0; CODE_LEN],
        message: [0; MESSAGE_LEN],
    }; 6];
    let n = get_all(&s.ring, s.head, s.count, &mut out);
    assert_eq!(n, 6);
    for (i, expected) in ["C6", "C5", "C4", "C3", "C2", "C1"].iter().enumerate() {
        assert_eq!(code_of(&out[i]), *expected, "row {i}");
    }
}
