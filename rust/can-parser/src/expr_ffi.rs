use crate::expr::{EvalContext, MAX_TOKENS};
use crate::expr_compile::{compile, Binary, CompileError, Step, Unary};
use crate::expr_run::{run, MissingRef};
use crate::CAN_FRAME_MAX_BYTES;

const STEP_NUM: u8 = 0;
const STEP_V: u8 = 1;
const STEP_B: u8 = 2;
const STEP_UNARY: u8 = 3;
const STEP_BINARY: u8 = 4;
const STEP_ID: u8 = 5;

pub const MAX_REFS: usize = 8;

pub const COMPILE_ERR_LEX: i32 = -1;
pub const COMPILE_ERR_TOO_LONG: i32 = -2;
pub const COMPILE_ERR_SYNTAX: i32 = -3;
pub const COMPILE_ERR_TOO_DEEP: i32 = -4;

const UNARIES: [Unary; 5] = [
    Unary::Neg,
    Unary::Not,
    Unary::Floor,
    Unary::Ceil,
    Unary::Round,
];

const BINARIES: [Binary; 16] = [
    Binary::Or,
    Binary::Xor,
    Binary::And,
    Binary::Eq,
    Binary::Ne,
    Binary::Lt,
    Binary::Le,
    Binary::Gt,
    Binary::Ge,
    Binary::Shl,
    Binary::Shr,
    Binary::Add,
    Binary::Sub,
    Binary::Mul,
    Binary::Div,
    Binary::Rem,
];

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct FfiTok {
    pub kind: u8,
    pub aux: u8,
    pub num: f32,
}

const fn ffi_tok(kind: u8, aux: u8, num: f32) -> FfiTok {
    FfiTok { kind, aux, num }
}

fn encode(step: Step) -> FfiTok {
    match step {
        Step::Num(n) => ffi_tok(STEP_NUM, 0, n),
        Step::V => ffi_tok(STEP_V, 0, 0.0),
        Step::B(idx) => ffi_tok(STEP_B, idx, 0.0),
        Step::Unary(op) => ffi_tok(STEP_UNARY, op as u8, 0.0),
        Step::Binary(op) => ffi_tok(STEP_BINARY, op as u8, 0.0),
        Step::Id(target_id) => ffi_tok(STEP_ID, 0, f32::from(target_id)),
    }
}

fn decode(tok: FfiTok) -> Option<Step> {
    let aux = usize::from(tok.aux);
    Some(match tok.kind {
        STEP_NUM => Step::Num(tok.num),
        STEP_V => Step::V,
        STEP_B if aux < CAN_FRAME_MAX_BYTES => Step::B(tok.aux),
        STEP_UNARY => Step::Unary(*UNARIES.get(aux)?),
        STEP_BINARY => Step::Binary(*BINARIES.get(aux)?),
        STEP_ID if tok.num >= 0.0 && tok.num <= f32::from(u16::MAX) => Step::Id(tok.num as u16),
        _ => return None,
    })
}

#[must_use]
pub fn compile_error_code(error: CompileError) -> i32 {
    match error {
        CompileError::Lex => COMPILE_ERR_LEX,
        CompileError::TooLong => COMPILE_ERR_TOO_LONG,
        CompileError::Syntax => COMPILE_ERR_SYNTAX,
        CompileError::TooDeep => COMPILE_ERR_TOO_DEEP,
    }
}

pub fn compile_to_ffi(expr: &[u8], out: &mut [FfiTok]) -> Result<usize, CompileError> {
    let mut steps = [Step::V; MAX_TOKENS];
    let n = compile(expr, &mut steps)?;
    let dst = out.get_mut(..n).ok_or(CompileError::TooLong)?;
    for (slot, step) in dst.iter_mut().zip(&steps[..n]) {
        *slot = encode(*step);
    }
    Ok(n)
}

fn run_ffi(tokens: &[FfiTok], ctx: &EvalContext, missing: MissingRef) -> Option<f32> {
    if tokens.len() > MAX_TOKENS {
        return None;
    }
    run(tokens.iter().map(|tok| decode(*tok)), ctx, missing)
}

#[must_use]
pub fn eval_ffi(tokens: &[FfiTok], ctx: &EvalContext) -> f32 {
    run_ffi(tokens, ctx, MissingRef::ReadsNan).unwrap_or(0.0)
}

#[must_use]
pub fn eval_ffi_checked(tokens: &[FfiTok], ctx: &EvalContext) -> f32 {
    run_ffi(tokens, ctx, MissingRef::Rejects).unwrap_or(f32::NAN)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::expr::eval;

    fn compiled(expr: &[u8]) -> Result<([FfiTok; MAX_TOKENS], usize), CompileError> {
        let mut toks = [ffi_tok(0, 0, 0.0); MAX_TOKENS];
        compile_to_ffi(expr, &mut toks).map(|n| (toks, n))
    }

    #[test]
    fn compiled_matches_oneshot() {
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
        ];
        let c = EvalContext {
            refs: &[],
            v: 0xD7 as f32,
            bytes: &bytes,
        };
        for e in exprs {
            let (toks, n) = compiled(e).expect("compiles");
            assert_eq!(eval(e, &c), eval_ffi(&toks[..n], &c), "expr {e:?}");
        }
    }

    #[test]
    fn every_operator_round_trips_through_the_ffi_encoding() {
        for op in UNARIES {
            assert_eq!(decode(encode(Step::Unary(op))), Some(Step::Unary(op)));
        }
        for op in BINARIES {
            assert_eq!(decode(encode(Step::Binary(op))), Some(Step::Binary(op)));
        }
        assert_eq!(decode(encode(Step::Id(481))), Some(Step::Id(481)));
    }

    #[test]
    fn corrupt_tokens_evaluate_to_the_failure_value() {
        let c = EvalContext {
            refs: &[],
            v: 1.0,
            bytes: &[],
        };
        let bad_op = [ffi_tok(STEP_V, 0, 0.0), ffi_tok(STEP_UNARY, 99, 0.0)];
        assert_eq!(eval_ffi(&bad_op, &c), 0.0);
        assert!(eval_ffi_checked(&bad_op, &c).is_nan());
        let underflow = [ffi_tok(STEP_BINARY, Binary::Add as u8, 0.0)];
        assert_eq!(eval_ffi(&underflow, &c), 0.0);
        assert_eq!(eval_ffi(&[], &c), 0.0);
    }

    #[test]
    fn compile_reports_each_failure_kind() {
        assert_eq!(compiled(b"@@@").err(), Some(CompileError::Lex));
        assert_eq!(compiled(b"V+").err(), Some(CompileError::Syntax));
        assert_eq!(compiled(b"").err(), Some(CompileError::Syntax));
        let mut short = [ffi_tok(0, 0, 0.0); 2];
        assert_eq!(
            compile_to_ffi(b"1+2", &mut short).err(),
            Some(CompileError::TooLong)
        );
    }
}
