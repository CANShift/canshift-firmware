use super::*;

#[test]
fn dismiss_at_newest_matches_dismiss_latest() {
    let mut s = Store::new();

    s.push(1, "A", "");
    s.push(2, "B", "");
    let v_before = s.version;

    dismiss_at(&mut s.ring, &mut s.head, &mut s.count, &mut s.version, 0);
    assert_eq!(s.count, 1);
    assert_eq!(s.version, v_before + 1);
    let mut out = [FwError {
        source: 0,
        code: [0; CODE_LEN],
        message: [0; MESSAGE_LEN],
    }; 1];
    assert_eq!(get_all(&s.ring, s.head, s.count, &mut out), 1);
    assert_eq!(code_of(&out[0]), "A");
}

#[test]
fn dismiss_at_oldest_advances_head() {
    let mut s = Store::new();

    s.push(1, "A", "");
    s.push(2, "B", "");
    s.push(3, "C", "");
    dismiss_at(&mut s.ring, &mut s.head, &mut s.count, &mut s.version, 2);
    assert_eq!(s.count, 2);
    assert_eq!(s.head, 1);
}

#[test]
fn dismiss_at_middle_collapses_gap() {
    let mut s = Store::new();

    s.push(1, "A", "");
    s.push(2, "B", "");
    s.push(3, "C", "");
    dismiss_at(&mut s.ring, &mut s.head, &mut s.count, &mut s.version, 1);
    assert_eq!(s.count, 2);
    let mut out = [FwError {
        source: 0,
        code: [0; CODE_LEN],
        message: [0; MESSAGE_LEN],
    }; 2];
    assert_eq!(get_all(&s.ring, s.head, s.count, &mut out), 2);
    assert_eq!(code_of(&out[0]), "C");
    assert_eq!(code_of(&out[1]), "A");
}

#[test]
fn dismiss_at_out_of_range_is_noop() {
    let mut s = Store::new();

    s.push(1, "A", "");
    let v_before = s.version;
    dismiss_at(&mut s.ring, &mut s.head, &mut s.count, &mut s.version, 99);
    assert_eq!(s.count, 1);
    assert_eq!(s.version, v_before);
}

#[test]
fn dismiss_at_empty_is_noop() {
    let mut s = Store::new();

    dismiss_at(&mut s.ring, &mut s.head, &mut s.count, &mut s.version, 0);
    assert_eq!(s.count, 0);
    assert_eq!(s.version, 0);
}
