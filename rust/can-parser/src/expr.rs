//! Recursive-descent evaluator for the safe CAN conversion expression grammar
//! (#1503). Mirrors canshift-core/src/can-xml/eval-expr.ts — same operator
//! precedence, same semantics, fixture-tested for parity.

use core::str;

const MAX_TOKENS: usize = 64;

#[derive(Clone, Copy)]
enum TokKind {
    Num(f32),
    V,
    B(u8),
    Fn(FnKind),
    Op(Op),
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum FnKind {
    Floor,
    Ceil,
    Round,
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum Op {
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

pub struct EvalContext<'a> {
    pub v: f32,
    pub bytes: &'a [u8],
}

fn lex(expr: &[u8], tokens: &mut [TokKind; MAX_TOKENS]) -> Option<usize> {
    let mut n = 0usize;
    let mut i = 0usize;
    while i < expr.len() {
        let c = expr[i];
        if c.is_ascii_whitespace() {
            i += 1;
            continue;
        }
        let two = if i + 1 < expr.len() {
            Some([c, expr[i + 1]])
        } else {
            None
        };
        let (op, consumed) = match two {
            Some([b'<', b'<']) => (Some(Op::Shl), 2),
            Some([b'>', b'>']) => (Some(Op::Shr), 2),
            Some([b'<', b'=']) => (Some(Op::Le), 2),
            Some([b'>', b'=']) => (Some(Op::Ge), 2),
            Some([b'=', b'=']) => (Some(Op::Eq), 2),
            Some([b'!', b'=']) => (Some(Op::Ne), 2),
            _ => (None, 0),
        };
        if let Some(op) = op {
            if n >= MAX_TOKENS {
                return None;
            }
            tokens[n] = TokKind::Op(op);
            n += 1;
            i += consumed;
            continue;
        }
        let single = match c {
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
        };
        if let Some(op) = single {
            if n >= MAX_TOKENS {
                return None;
            }
            tokens[n] = TokKind::Op(op);
            n += 1;
            i += 1;
            continue;
        }
        if c == b'0' && i + 1 < expr.len() && (expr[i + 1] == b'x' || expr[i + 1] == b'X') {
            let mut j = i + 2;
            let mut acc: u32 = 0;
            while j < expr.len() {
                let h = expr[j];
                let d = match h {
                    b'0'..=b'9' => (h - b'0') as u32,
                    b'a'..=b'f' => 10 + (h - b'a') as u32,
                    b'A'..=b'F' => 10 + (h - b'A') as u32,
                    _ => break,
                };
                acc = acc.wrapping_mul(16).wrapping_add(d);
                j += 1;
            }
            if n >= MAX_TOKENS {
                return None;
            }
            tokens[n] = TokKind::Num(acc as f32);
            n += 1;
            i = j;
            continue;
        }
        if c.is_ascii_digit() || c == b'.' {
            let start = i;
            while i < expr.len() && (expr[i].is_ascii_digit() || expr[i] == b'.') {
                i += 1;
            }
            let s = str::from_utf8(&expr[start..i]).ok()?;
            let num: f32 = s.parse().ok()?;
            if n >= MAX_TOKENS {
                return None;
            }
            tokens[n] = TokKind::Num(num);
            n += 1;
            continue;
        }
        if c.is_ascii_alphabetic() || c == b'_' {
            let start = i;
            while i < expr.len()
                && (expr[i].is_ascii_alphanumeric() || expr[i] == b'_')
            {
                i += 1;
            }
            let ident = &expr[start..i];
            let tok = match ident {
                b"V" => TokKind::V,
                b"B0" => TokKind::B(0),
                b"B1" => TokKind::B(1),
                b"B2" => TokKind::B(2),
                b"B3" => TokKind::B(3),
                b"B4" => TokKind::B(4),
                b"B5" => TokKind::B(5),
                b"B6" => TokKind::B(6),
                b"B7" => TokKind::B(7),
                b"Floor" => TokKind::Fn(FnKind::Floor),
                b"Ceil" => TokKind::Fn(FnKind::Ceil),
                b"Round" => TokKind::Fn(FnKind::Round),
                _ => return None,
            };
            if n >= MAX_TOKENS {
                return None;
            }
            tokens[n] = tok;
            n += 1;
            continue;
        }
        return None;
    }
    Some(n)
}

struct ParseState<'a> {
    tokens: &'a [TokKind],
    pos: usize,
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

fn parse_or(s: &mut ParseState, ctx: &EvalContext) -> Option<f32> {
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
            Some(if inner == 0.0 { 1.0 } else { 0.0 })
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
    if result.is_finite() { result } else { 0.0 }
}

const TOK_NUM: u8 = 0;
const TOK_V: u8 = 1;
const TOK_B: u8 = 2;
const TOK_FN: u8 = 3;
const TOK_OP: u8 = 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiTok {
    pub kind: u8,
    pub aux: u8,
    pub num: f32,
}

fn fn_from_u8(aux: u8) -> Option<FnKind> {
    match aux {
        0 => Some(FnKind::Floor),
        1 => Some(FnKind::Ceil),
        2 => Some(FnKind::Round),
        _ => None,
    }
}

fn op_from_u8(aux: u8) -> Option<Op> {
    match aux {
        0 => Some(Op::LParen),
        1 => Some(Op::RParen),
        2 => Some(Op::Comma),
        3 => Some(Op::Plus),
        4 => Some(Op::Minus),
        5 => Some(Op::Star),
        6 => Some(Op::Slash),
        7 => Some(Op::Percent),
        8 => Some(Op::Shl),
        9 => Some(Op::Shr),
        10 => Some(Op::Lt),
        11 => Some(Op::Le),
        12 => Some(Op::Gt),
        13 => Some(Op::Ge),
        14 => Some(Op::Eq),
        15 => Some(Op::Ne),
        16 => Some(Op::And),
        17 => Some(Op::Or),
        18 => Some(Op::Xor),
        19 => Some(Op::Bang),
        _ => None,
    }
}

fn tok_to_ffi(t: TokKind) -> FfiTok {
    match t {
        TokKind::Num(n) => FfiTok { kind: TOK_NUM, aux: 0, num: n },
        TokKind::V => FfiTok { kind: TOK_V, aux: 0, num: 0.0 },
        TokKind::B(idx) => FfiTok { kind: TOK_B, aux: idx, num: 0.0 },
        TokKind::Fn(f) => FfiTok { kind: TOK_FN, aux: f as u8, num: 0.0 },
        TokKind::Op(o) => FfiTok { kind: TOK_OP, aux: o as u8, num: 0.0 },
    }
}

fn ffi_to_tok(f: FfiTok) -> Option<TokKind> {
    Some(match f.kind {
        TOK_NUM => TokKind::Num(f.num),
        TOK_V => TokKind::V,
        TOK_B if f.aux < crate::CAN_FRAME_MAX_BYTES as u8 => TokKind::B(f.aux),
        TOK_FN => TokKind::Fn(fn_from_u8(f.aux)?),
        TOK_OP => TokKind::Op(op_from_u8(f.aux)?),
        _ => return None,
    })
}

pub fn lex_to_ffi(expr: &[u8], out: &mut [FfiTok]) -> Option<usize> {
    let mut tokens = [TokKind::V; MAX_TOKENS];
    let n = lex(expr, &mut tokens)?;
    if out.len() < n {
        return None;
    }
    for i in 0..n {
        out[i] = tok_to_ffi(tokens[i]);
    }
    Some(n)
}

#[must_use]
pub fn eval_ffi(tokens: &[FfiTok], ctx: &EvalContext) -> f32 {
    let n = tokens.len();
    if n == 0 || n > MAX_TOKENS {
        return 0.0;
    }
    let mut buf = [TokKind::V; MAX_TOKENS];
    for i in 0..n {
        match ffi_to_tok(tokens[i]) {
            Some(t) => buf[i] = t,
            None => return 0.0,
        }
    }
    let mut state = ParseState { tokens: &buf[..n], pos: 0 };
    let Some(result) = parse_or(&mut state, ctx) else {
        return 0.0;
    };
    if state.pos != n {
        return 0.0;
    }
    if result.is_finite() { result } else { 0.0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctx(v: f32, bytes: &[u8]) -> EvalContext {
        EvalContext { v, bytes }
    }

    fn eval_tokenized(expr: &[u8], ctx: &EvalContext) -> f32 {
        let mut toks = [FfiTok { kind: 0, aux: 0, num: 0.0 }; MAX_TOKENS];
        match lex_to_ffi(expr, &mut toks) {
            Some(n) => eval_ffi(&toks[..n], ctx),
            None => 0.0,
        }
    }

    #[test]
    fn tokenized_matches_oneshot() {
        let bytes = [117u8, 20, 30, 40, 50, 60, 70, 80];
        let exprs: &[&[u8]] = &[
            b"42",
            b"V",
            b"0xFF",
            b"B0",
            b"B7",
            b"2+3*4",
            b"V==0xD7",
            b"1<<4",
            b"0xFF&0x0F",
            b"Floor(3.7)",
            b"(Floor(V/200)/2)*100",
            b"(V==0xD7)|(V==0xEF)",
            b"14.7*(B0/117)",
            b"",
            b"@@@",
        ];
        for e in exprs {
            let c = ctx(0xD7 as f32, &bytes);
            assert_eq!(eval(e, &c), eval_tokenized(e, &c), "expr {e:?}");
        }
    }

    #[test]
    fn numbers_and_v() {
        assert_eq!(eval(b"42", &ctx(0.0, &[])), 42.0);
        assert_eq!(eval(b"V", &ctx(7.0, &[])), 7.0);
        assert_eq!(eval(b"0xFF", &ctx(0.0, &[])), 255.0);
    }

    #[test]
    fn byte_refs() {
        let bytes = [10u8, 20, 30, 40, 50, 60, 70, 80];
        assert_eq!(eval(b"B0", &ctx(0.0, &bytes)), 10.0);
        assert_eq!(eval(b"B7", &ctx(0.0, &bytes)), 80.0);
    }

    #[test]
    fn arithmetic() {
        assert_eq!(eval(b"6*7", &ctx(0.0, &[])), 42.0);
        assert_eq!(eval(b"20/4", &ctx(0.0, &[])), 5.0);
        assert_eq!(eval(b"2+3*4", &ctx(0.0, &[])), 14.0);
    }

    #[test]
    fn comparisons() {
        assert_eq!(eval(b"V==0xD7", &ctx(0xD7 as f32, &[])), 1.0);
        assert_eq!(eval(b"V==0xD7", &ctx(0.0, &[])), 0.0);
        assert_eq!(eval(b"V>10", &ctx(20.0, &[])), 1.0);
    }

    #[test]
    fn bit_ops_and_shifts() {
        assert_eq!(eval(b"1<<4", &ctx(0.0, &[])), 16.0);
        assert_eq!(eval(b"256>>4", &ctx(0.0, &[])), 16.0);
        assert_eq!(eval(b"0xFF&0x0F", &ctx(0.0, &[])), 15.0);
        assert_eq!(eval(b"0xF0|0x0F", &ctx(0.0, &[])), 255.0);
    }

    #[test]
    fn functions() {
        assert_eq!(eval(b"Floor(3.7)", &ctx(0.0, &[])), 3.0);
        assert_eq!(eval(b"Ceil(3.2)", &ctx(0.0, &[])), 4.0);
        assert_eq!(eval(b"Round(3.5)", &ctx(0.0, &[])), 4.0);
        assert_eq!(eval(b"(Floor(V/200)/2)*100", &ctx(401.0, &[])), 100.0);
    }

    #[test]
    fn catalogue_patterns() {
        assert_eq!(eval(b"(V==0xD7)|(V==0xEF)", &ctx(0xD7 as f32, &[])), 1.0);
        assert_eq!(eval(b"(V==0xD7)|(V==0xEF)", &ctx(0.0, &[])), 0.0);
        assert_eq!(eval(b"(V&1)*100", &ctx(1.0, &[])), 100.0);

        let bytes = [117u8];
        let r = eval(b"14.7*(B0/117)", &ctx(0.0, &bytes));
        assert!((r - 14.7).abs() < 1e-4);
    }

    #[test]
    fn error_paths() {
        assert_eq!(eval(b"", &ctx(0.0, &[])), 0.0);
        assert_eq!(eval(b"@@@", &ctx(0.0, &[])), 0.0);
    }
}
