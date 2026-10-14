use core::str;

pub(crate) const MAX_TOKENS: usize = 64;

#[derive(Clone, Copy)]
pub(crate) enum TokKind {
    Num(f32),
    V,
    B(u8),
    Fn(FnKind),
    Op(Op),
    Id(u16),
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum FnKind {
    Floor,
    Ceil,
    Round,
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum Op {
    LParen,
    RParen,
    Comma,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Shl,
    Shr,
    Lt,
    Le,
    Gt,
    Ge,
    Eq,
    Ne,
    And,
    Or,
    Xor,
    Bang,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RefValue {
    pub target_id: u16,
    pub value: f32,
}

pub struct EvalContext<'a> {
    pub v: f32,
    pub bytes: &'a [u8],
    pub refs: &'a [RefValue],
}

pub use crate::expr_run::{eval, eval_checked};

fn lex_two_char_op(a: u8, b: u8) -> Option<Op> {
    match [a, b] {
        [b'<', b'<'] => Some(Op::Shl),
        [b'>', b'>'] => Some(Op::Shr),
        [b'<', b'='] => Some(Op::Le),
        [b'>', b'='] => Some(Op::Ge),
        [b'=', b'='] => Some(Op::Eq),
        [b'!', b'='] => Some(Op::Ne),
        _ => None,
    }
}

fn lex_single_char_op(c: u8) -> Option<Op> {
    match c {
        b'(' => Some(Op::LParen),
        b')' => Some(Op::RParen),
        b',' => Some(Op::Comma),
        b'+' => Some(Op::Plus),
        b'-' => Some(Op::Minus),
        b'*' => Some(Op::Star),
        b'/' => Some(Op::Slash),
        b'%' => Some(Op::Percent),
        b'<' => Some(Op::Lt),
        b'>' => Some(Op::Gt),
        b'&' => Some(Op::And),
        b'|' => Some(Op::Or),
        b'^' => Some(Op::Xor),
        b'!' => Some(Op::Bang),
        _ => None,
    }
}

fn is_hex_start(expr: &[u8], i: usize) -> bool {
    expr[i] == b'0' && matches!(expr.get(i + 1), Some(b'x') | Some(b'X'))
}

fn lex_hex(expr: &[u8], i: usize) -> (TokKind, usize) {
    let mut j = i + 2;
    let mut acc: u32 = 0;
    while j < expr.len() {
        let d = match expr[j] {
            h @ b'0'..=b'9' => u32::from(h - b'0'),
            h @ b'a'..=b'f' => 10 + u32::from(h - b'a'),
            h @ b'A'..=b'F' => 10 + u32::from(h - b'A'),
            _ => break,
        };
        acc = acc.wrapping_mul(16).wrapping_add(d);
        j += 1;
    }
    (TokKind::Num(acc as f32), j)
}

fn lex_decimal(expr: &[u8], start: usize) -> Option<(TokKind, usize)> {
    let mut i = start;
    while i < expr.len() && (expr[i].is_ascii_digit() || expr[i] == b'.') {
        i += 1;
    }
    let s = str::from_utf8(&expr[start..i]).ok()?;
    let num: f32 = s.parse().ok()?;
    Some((TokKind::Num(num), i))
}

fn lex_target_id(word: &[u8]) -> Option<u16> {
    let digits = match word {
        [b'I' | b'i', b'D' | b'd', rest @ ..] if !rest.is_empty() => rest,
        _ => return None,
    };
    let mut acc: u32 = 0;
    for d in digits {
        if !d.is_ascii_digit() {
            return None;
        }
        acc = acc.checked_mul(10)?.checked_add(u32::from(d - b'0'))?;
        if acc > u32::from(u16::MAX) {
            return None;
        }
    }
    Some(acc as u16)
}

fn lex_ident(expr: &[u8], start: usize) -> Option<(TokKind, usize)> {
    let mut i = start;
    while i < expr.len() && (expr[i].is_ascii_alphanumeric() || expr[i] == b'_') {
        i += 1;
    }
    if let Some(id) = lex_target_id(&expr[start..i]) {
        return Some((TokKind::Id(id), i));
    }
    let tok = match &expr[start..i] {
        b"V" => TokKind::V,
        [b'B', d @ b'0'..=b'7'] => TokKind::B(d - b'0'),
        b"Floor" => TokKind::Fn(FnKind::Floor),
        b"Ceil" => TokKind::Fn(FnKind::Ceil),
        b"Round" => TokKind::Fn(FnKind::Round),
        _ => return None,
    };
    Some((tok, i))
}

fn lex_token_at(expr: &[u8], i: usize) -> Option<(TokKind, usize)> {
    let c = expr[i];
    if let Some(op) = expr.get(i + 1).and_then(|&b| lex_two_char_op(c, b)) {
        return Some((TokKind::Op(op), i + 2));
    }
    if let Some(op) = lex_single_char_op(c) {
        return Some((TokKind::Op(op), i + 1));
    }
    if is_hex_start(expr, i) {
        return Some(lex_hex(expr, i));
    }
    if c.is_ascii_digit() || c == b'.' {
        return lex_decimal(expr, i);
    }
    if c.is_ascii_alphabetic() || c == b'_' {
        return lex_ident(expr, i);
    }
    None
}

pub(crate) struct Tokens<'a> {
    expr: &'a [u8],
    pos: usize,
}

impl<'a> Tokens<'a> {
    pub(crate) fn new(expr: &'a [u8]) -> Self {
        Self { expr, pos: 0 }
    }
}

impl Iterator for Tokens<'_> {
    type Item = Option<TokKind>;

    fn next(&mut self) -> Option<Self::Item> {
        while self.expr.get(self.pos)?.is_ascii_whitespace() {
            self.pos += 1;
        }
        let Some((tok, next)) = lex_token_at(self.expr, self.pos) else {
            self.pos = self.expr.len();
            return Some(None);
        };
        self.pos = next;
        Some(Some(tok))
    }
}
