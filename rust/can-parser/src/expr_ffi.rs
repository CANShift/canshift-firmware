//! FFI token mapping for the expression evaluator — the tokenized form the C++
//! side caches per signal so hot-path evals skip the lexer.

use crate::expr::{lex, parse_or, EvalContext, FnKind, Op, ParseState, TokKind, MAX_TOKENS};

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
        TokKind::Num(n) => FfiTok {
            kind: TOK_NUM,
            aux: 0,
            num: n,
        },
        TokKind::V => FfiTok {
            kind: TOK_V,
            aux: 0,
            num: 0.0,
        },
        TokKind::B(idx) => FfiTok {
            kind: TOK_B,
            aux: idx,
            num: 0.0,
        },
        TokKind::Fn(f) => FfiTok {
            kind: TOK_FN,
            aux: f as u8,
            num: 0.0,
        },
        TokKind::Op(o) => FfiTok {
            kind: TOK_OP,
            aux: o as u8,
            num: 0.0,
        },
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
    let mut state = ParseState {
        tokens: &buf[..n],
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
    use crate::expr::eval;

    fn eval_tokenized(expr: &[u8], ctx: &EvalContext) -> f32 {
        let mut toks = [FfiTok {
            kind: 0,
            aux: 0,
            num: 0.0,
        }; MAX_TOKENS];
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
            let c = EvalContext {
                v: 0xD7 as f32,
                bytes: &bytes,
            };
            assert_eq!(eval(e, &c), eval_tokenized(e, &c), "expr {e:?}");
        }
    }
}
