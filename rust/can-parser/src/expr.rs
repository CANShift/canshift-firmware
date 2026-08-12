//! Recursive-descent evaluator for the safe CAN conversion expression grammar
//! (#1503). Mirrors canshift-core/src/can-xml/eval-expr.ts — same operator
//! precedence, same semantics, fixture-tested for parity.

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

pub(crate) fn lex(expr: &[u8], tokens: &mut [TokKind; MAX_TOKENS]) -> Option<usize> {
    let mut n = 0usize;
    let mut i = 0usize;
    while i < expr.len() {
        if expr[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }
        let (tok, next) = lex_token_at(expr, i)?;
        if n >= MAX_TOKENS {
            return None;
        }
        tokens[n] = tok;
        n += 1;
        i = next;
    }
    Some(n)
}

pub(crate) struct ParseState<'a> {
    pub(crate) tokens: &'a [TokKind],
    pub(crate) pos: usize,
}

impl<'a> ParseState<'a> {
    fn peek_op(&self) -> Option<Op> {
        match self.tokens.get(self.pos)? {
            TokKind::Op(o) => Some(*o),
            _ => None,
        }
    }
    fn eat(&mut self, op: Op) -> bool {
        if self.peek_op() == Some(op) {
            self.pos += 1;
            true
        } else {
            false
        }
    }
}

fn read_byte(ctx: &EvalContext, idx: u8) -> f32 {
    ctx.bytes.get(idx as usize).copied().unwrap_or(0) as f32
}

// Absent reference evaluates to NaN so the whole expression is non-finite:
// substituting 0 would read as "no fault" when the truth is "unknown".
fn read_ref(ctx: &EvalContext, target_id: u16) -> f32 {
    for r in ctx.refs {
        if r.target_id == target_id {
            return r.value;
        }
    }
    f32::NAN
}

pub(crate) fn parse_or(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_xor(s, ctx)?;
    while s.peek_op() == Some(Op::Or) {
        s.pos += 1;
        let right = parse_xor(s, ctx)?;
        left = ((left as u32) | (right as u32)) as f32;
    }
    Some(left)
}

fn parse_xor(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_and(s, ctx)?;
    while s.peek_op() == Some(Op::Xor) {
        s.pos += 1;
        let right = parse_and(s, ctx)?;
        left = ((left as u32) ^ (right as u32)) as f32;
    }
    Some(left)
}

fn parse_and(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_eq(s, ctx)?;
    while s.peek_op() == Some(Op::And) {
        s.pos += 1;
        let right = parse_eq(s, ctx)?;
        left = ((left as u32) & (right as u32)) as f32;
    }
    Some(left)
}

fn parse_eq(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_rel(s, ctx)?;
    while matches!(s.peek_op(), Some(Op::Eq) | Some(Op::Ne)) {
        let op = s.peek_op()?;
        s.pos += 1;
        let right = parse_rel(s, ctx)?;
        left = match op {
            Op::Eq => {
                if left == right {
                    1.0
                } else {
                    0.0
                }
            }
            _ => {
                if left != right {
                    1.0
                } else {
                    0.0
                }
            }
        };
    }
    Some(left)
}

fn parse_rel(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_shift(s, ctx)?;
    while matches!(
        s.peek_op(),
        Some(Op::Lt) | Some(Op::Le) | Some(Op::Gt) | Some(Op::Ge)
    ) {
        let op = s.peek_op()?;
        s.pos += 1;
        let right = parse_shift(s, ctx)?;
        left = match op {
            Op::Lt => {
                if left < right {
                    1.0
                } else {
                    0.0
                }
            }
            Op::Le => {
                if left <= right {
                    1.0
                } else {
                    0.0
                }
            }
            Op::Gt => {
                if left > right {
                    1.0
                } else {
                    0.0
                }
            }
            _ => {
                if left >= right {
                    1.0
                } else {
                    0.0
                }
            }
        };
    }
    Some(left)
}

fn parse_shift(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_add(s, ctx)?;
    while matches!(s.peek_op(), Some(Op::Shl) | Some(Op::Shr)) {
        let op = s.peek_op()?;
        s.pos += 1;
        let right = parse_add(s, ctx)?;
        let l = left as u32;
        let r = right as u32 & 31;
        left = if op == Op::Shl {
            (l << r) as f32
        } else {
            (l >> r) as f32
        };
    }
    Some(left)
}

fn parse_add(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_mul(s, ctx)?;
    while matches!(s.peek_op(), Some(Op::Plus) | Some(Op::Minus)) {
        let op = s.peek_op()?;
        s.pos += 1;
        let right = parse_mul(s, ctx)?;
        left = if op == Op::Plus {
            left + right
        } else {
            left - right
        };
    }
    Some(left)
}

fn parse_mul(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let mut left = parse_unary(s, ctx)?;
    while matches!(
        s.peek_op(),
        Some(Op::Star) | Some(Op::Slash) | Some(Op::Percent)
    ) {
        let op = s.peek_op()?;
        s.pos += 1;
        let right = parse_unary(s, ctx)?;
        left = match op {
            Op::Star => left * right,
            Op::Slash => left / right,
            _ => left % right,
        };
    }
    Some(left)
}

fn parse_unary(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    match s.peek_op() {
        Some(Op::Minus) => {
            s.pos += 1;
            Some(-parse_unary(s, ctx)?)
        }
        Some(Op::Plus) => {
            s.pos += 1;
            parse_unary(s, ctx)
        }
        Some(Op::Bang) => {
            s.pos += 1;
            let inner = parse_unary(s, ctx)?;
            Some(if inner == 0.0 {
                1.0
            } else {
                0.0
            })
        }
        _ => parse_primary(s, ctx),
    }
}

fn parse_primary(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
    let t = s.tokens.get(s.pos)?;
    match t {
        TokKind::Op(Op::LParen) => {
            s.pos += 1;
            let inner = parse_or(s, ctx)?;
            if !s.eat(Op::RParen) {
                return None;
            }
            Some(inner)
        }
        TokKind::Num(n) => {
            let v = *n;
            s.pos += 1;
            Some(v)
        }
        TokKind::V => {
            s.pos += 1;
            Some(ctx.v)
        }
        TokKind::B(idx) => {
            let i = *idx;
            s.pos += 1;
            Some(read_byte(ctx, i))
        }
        TokKind::Id(target_id) => {
            let id = *target_id;
            s.pos += 1;
            Some(read_ref(ctx, id))
        }
        TokKind::Fn(kind) => {
            let k = *kind;
            s.pos += 1;
            if !s.eat(Op::LParen) {
                return None;
            }
            let arg = parse_or(s, ctx)?;
            if !s.eat(Op::RParen) {
                return None;
            }
            Some(match k {
                FnKind::Floor => libm_floor(arg),
                FnKind::Ceil => libm_ceil(arg),
                FnKind::Round => libm_round(arg),
            })
        }
        TokKind::Op(_) => None,
    }
}

fn libm_floor(x: f32) -> f32 {
    let trunc = (x as i64) as f32;
    if x < 0.0 && (x - trunc) != 0.0 {
        trunc - 1.0
    } else {
        trunc
    }
}

fn libm_ceil(x: f32) -> f32 {
    let trunc = (x as i64) as f32;
    if x > 0.0 && (x - trunc) != 0.0 {
        trunc + 1.0
    } else {
        trunc
    }
}

fn libm_round(x: f32) -> f32 {
    if x >= 0.0 {
        libm_floor(x + 0.5)
    } else {
        libm_ceil(x - 0.5)
    }
}

// A missing reference cannot be signalled by NaN alone: the bitwise operators
// cast to u32, and NaN casts to 0 — which reads as "no fault" on an OR over
// fault flags. Unresolved references fail the whole expression instead.
#[must_use]
pub(crate) fn all_refs_resolve(tokens: &[TokKind], ctx: &EvalContext) -> bool {
    tokens.iter().all(|t| match t {
        TokKind::Id(target_id) => !read_ref(ctx, *target_id).is_nan(),
        _ => true,
    })
}

#[must_use]
pub fn eval_checked(expr: &[u8], ctx: &EvalContext) -> f32 {
    let mut tokens = [TokKind::V; MAX_TOKENS];
    let Some(n) = lex(expr, &mut tokens) else {
        return f32::NAN;
    };
    if n == 0 || !all_refs_resolve(&tokens[..n], ctx) {
        return f32::NAN;
    }
    let mut state = ParseState {
        tokens: &tokens[..n],
        pos: 0,
    };
    let Some(result) = parse_or(&mut state, ctx) else {
        return f32::NAN;
    };
    if state.pos != n {
        return f32::NAN;
    }
    if result.is_finite() {
        result
    } else {
        f32::NAN
    }
}

#[must_use]
pub fn eval(expr: &[u8], ctx: &EvalContext) -> f32 {
    let mut tokens = [TokKind::V; MAX_TOKENS];
    let Some(n) = lex(expr, &mut tokens) else {
        return 0.0;
    };
    if n == 0 {
        return 0.0;
    }
    let mut state = ParseState {
        tokens: &tokens[..n],
        pos: 0,
    };
    let Some(result) = parse_or(&mut state, ctx) else {
        return 0.0;
    };
    if state.pos != n {
        return 0.0;
    }
    if result.is_finite() {
        result
    } else {
        0.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctx<'a>(v: f32, bytes: &'a [u8], refs: &'a [RefValue]) -> EvalContext<'a> {
        EvalContext { v, bytes, refs }
    }

    fn ctx_no_refs<'a>(v: f32, bytes: &'a [u8]) -> EvalContext<'a> {
        ctx(v, bytes, &[])
    }

    #[test]
    fn target_id_reads_a_reference() {
        let refs = [RefValue {
            target_id: 37,
            value: 41.0,
        }];
        assert_eq!(eval(b"ID37+1", &ctx(0.0, &[], &refs)), 42.0);
        assert_eq!(eval_checked(b"ID37+1", &ctx(0.0, &[], &refs)), 42.0);
    }

    #[test]
    fn missing_reference_poisons_the_expression() {
        let refs = [RefValue {
            target_id: 481,
            value: 1.0,
        }];
        assert!(eval_checked(b"ID481|ID482", &ctx(0.0, &[], &refs)).is_nan());
        assert!(eval_checked(b"ID999", &ctx(0.0, &[], &[])).is_nan());
        assert_eq!(eval_checked(b"ID481|ID481", &ctx(0.0, &[], &refs)), 1.0);
    }

    #[test]
    fn target_id_is_case_insensitive_and_bounded() {
        let refs = [RefValue {
            target_id: 7,
            value: 3.0,
        }];
        assert_eq!(eval(b"id7", &ctx(0.0, &[], &refs)), 3.0);
        assert_eq!(eval(b"ID70000", &ctx(0.0, &[], &refs)), 0.0);
    }

    #[test]
    fn numbers_and_v() {
        assert_eq!(eval(b"42", &ctx_no_refs(0.0, &[])), 42.0);
        assert_eq!(eval(b"V", &ctx_no_refs(7.0, &[])), 7.0);
        assert_eq!(eval(b"0xFF", &ctx_no_refs(0.0, &[])), 255.0);
    }

    #[test]
    fn byte_refs() {
        let bytes = [10u8, 20, 30, 40, 50, 60, 70, 80];
        assert_eq!(eval(b"B0", &ctx_no_refs(0.0, &bytes)), 10.0);
        assert_eq!(eval(b"B7", &ctx_no_refs(0.0, &bytes)), 80.0);
    }

    #[test]
    fn arithmetic() {
        assert_eq!(eval(b"6*7", &ctx_no_refs(0.0, &[])), 42.0);
        assert_eq!(eval(b"20/4", &ctx_no_refs(0.0, &[])), 5.0);
        assert_eq!(eval(b"2+3*4", &ctx_no_refs(0.0, &[])), 14.0);
    }

    #[test]
    fn comparisons() {
        assert_eq!(eval(b"V==0xD7", &ctx_no_refs(0xD7 as f32, &[])), 1.0);
        assert_eq!(eval(b"V==0xD7", &ctx_no_refs(0.0, &[])), 0.0);
        assert_eq!(eval(b"V>10", &ctx_no_refs(20.0, &[])), 1.0);
    }

    #[test]
    fn bit_ops_and_shifts() {
        assert_eq!(eval(b"1<<4", &ctx_no_refs(0.0, &[])), 16.0);
        assert_eq!(eval(b"256>>4", &ctx_no_refs(0.0, &[])), 16.0);
        assert_eq!(eval(b"0xFF&0x0F", &ctx_no_refs(0.0, &[])), 15.0);
        assert_eq!(eval(b"0xF0|0x0F", &ctx_no_refs(0.0, &[])), 255.0);
    }

    #[test]
    fn functions() {
        assert_eq!(eval(b"Floor(3.7)", &ctx_no_refs(0.0, &[])), 3.0);
        assert_eq!(eval(b"Ceil(3.2)", &ctx_no_refs(0.0, &[])), 4.0);
        assert_eq!(eval(b"Round(3.5)", &ctx_no_refs(0.0, &[])), 4.0);
        assert_eq!(eval(b"(Floor(V/200)/2)*100", &ctx_no_refs(401.0, &[])), 100.0);
    }

    #[test]
    fn catalogue_patterns() {
        assert_eq!(eval(b"(V==0xD7)|(V==0xEF)", &ctx_no_refs(0xD7 as f32, &[])), 1.0);
        assert_eq!(eval(b"(V==0xD7)|(V==0xEF)", &ctx_no_refs(0.0, &[])), 0.0);
        assert_eq!(eval(b"(V&1)*100", &ctx_no_refs(1.0, &[])), 100.0);

        let bytes = [117u8];
        let r = eval(b"14.7*(B0/117)", &ctx_no_refs(0.0, &bytes));
        assert!((r - 14.7).abs() < 1e-4);
    }

    #[test]
    fn error_paths() {
        assert_eq!(eval(b"", &ctx_no_refs(0.0, &[])), 0.0);
        assert_eq!(eval(b"@@@", &ctx_no_refs(0.0, &[])), 0.0);
    }
}
