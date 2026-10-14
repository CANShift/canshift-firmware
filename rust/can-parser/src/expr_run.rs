use crate::expr::{EvalContext, MAX_TOKENS};
use crate::expr_compile::{compile, Binary, Step, Unary, MAX_STACK_DEPTH};

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum MissingRef {
    ReadsNan,
    Rejects,
}

fn truth(condition: bool) -> f32 {
    if condition {
        1.0
    } else {
        0.0
    }
}

fn floor(x: f32) -> f32 {
    let trunc = (x as i64) as f32;
    if x < 0.0 && (x - trunc) != 0.0 {
        trunc - 1.0
    } else {
        trunc
    }
}

fn ceil(x: f32) -> f32 {
    let trunc = (x as i64) as f32;
    if x > 0.0 && (x - trunc) != 0.0 {
        trunc + 1.0
    } else {
        trunc
    }
}

fn round(x: f32) -> f32 {
    if x >= 0.0 {
        floor(x + 0.5)
    } else {
        ceil(x - 0.5)
    }
}

fn apply_unary(op: Unary, x: f32) -> f32 {
    match op {
        Unary::Neg => -x,
        Unary::Not => truth(x == 0.0),
        Unary::Floor => floor(x),
        Unary::Ceil => ceil(x),
        Unary::Round => round(x),
    }
}

fn apply_binary(op: Binary, l: f32, r: f32) -> f32 {
    match op {
        Binary::Or => ((l as u32) | (r as u32)) as f32,
        Binary::Xor => ((l as u32) ^ (r as u32)) as f32,
        Binary::And => ((l as u32) & (r as u32)) as f32,
        Binary::Eq => truth(l == r),
        Binary::Ne => truth(l != r),
        Binary::Lt => truth(l < r),
        Binary::Le => truth(l <= r),
        Binary::Gt => truth(l > r),
        Binary::Ge => truth(l >= r),
        Binary::Shl => ((l as u32) << (r as u32 & 31)) as f32,
        Binary::Shr => ((l as u32) >> (r as u32 & 31)) as f32,
        Binary::Add => l + r,
        Binary::Sub => l - r,
        Binary::Mul => l * r,
        Binary::Div => l / r,
        Binary::Rem => l % r,
    }
}

fn read_ref(ctx: &EvalContext, target_id: u16) -> f32 {
    ctx.refs
        .iter()
        .find(|r| r.target_id == target_id)
        .map_or(f32::NAN, |r| r.value)
}

fn load(step: Step, ctx: &EvalContext, missing: MissingRef) -> Option<f32> {
    match step {
        Step::Num(n) => Some(n),
        Step::V => Some(ctx.v),
        Step::B(idx) => Some(f32::from(
            ctx.bytes.get(usize::from(idx)).copied().unwrap_or(0),
        )),
        Step::Id(target_id) => {
            let value = read_ref(ctx, target_id);
            (missing == MissingRef::ReadsNan || !value.is_nan()).then_some(value)
        }
        Step::Unary(_) | Step::Binary(_) => None,
    }
}

struct Machine {
    stack: [f32; MAX_STACK_DEPTH],
    len: usize,
}

impl Machine {
    fn push(&mut self, value: f32) -> Option<()> {
        *self.stack.get_mut(self.len)? = value;
        self.len += 1;
        Some(())
    }

    fn pop(&mut self) -> Option<f32> {
        self.len = self.len.checked_sub(1)?;
        Some(self.stack[self.len])
    }

    fn step(&mut self, step: Step, ctx: &EvalContext, missing: MissingRef) -> Option<()> {
        match step {
            Step::Unary(op) => {
                let x = self.pop()?;
                self.push(apply_unary(op, x))
            }
            Step::Binary(op) => {
                let r = self.pop()?;
                let l = self.pop()?;
                self.push(apply_binary(op, l, r))
            }
            _ => self.push(load(step, ctx, missing)?),
        }
    }
}

pub(crate) fn run<I>(steps: I, ctx: &EvalContext, missing: MissingRef) -> Option<f32>
where
    I: IntoIterator<Item = Option<Step>>,
{
    let mut machine = Machine {
        stack: [0.0; MAX_STACK_DEPTH],
        len: 0,
    };
    for step in steps {
        machine.step(step?, ctx, missing)?;
    }
    let result = (machine.len == 1).then_some(machine.stack[0])?;
    result.is_finite().then_some(result)
}

#[must_use]
pub fn evaluate(steps: &[Step], ctx: &EvalContext) -> f32 {
    run(steps.iter().copied().map(Some), ctx, MissingRef::ReadsNan).unwrap_or(0.0)
}

#[must_use]
pub fn evaluate_checked(steps: &[Step], ctx: &EvalContext) -> f32 {
    run(steps.iter().copied().map(Some), ctx, MissingRef::Rejects).unwrap_or(f32::NAN)
}

#[must_use]
pub fn eval(expr: &[u8], ctx: &EvalContext) -> f32 {
    let mut steps = [Step::V; MAX_TOKENS];
    compile(expr, &mut steps).map_or(0.0, |n| evaluate(&steps[..n], ctx))
}

#[must_use]
pub fn eval_checked(expr: &[u8], ctx: &EvalContext) -> f32 {
    let mut steps = [Step::V; MAX_TOKENS];
    compile(expr, &mut steps).map_or(f32::NAN, |n| evaluate_checked(&steps[..n], ctx))
}
