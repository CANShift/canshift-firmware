use crate::expr::{FnKind, Op, TokKind, Tokens, MAX_TOKENS};

pub const MAX_STACK_DEPTH: usize = 16;

const PREFIX_PRECEDENCE: u8 = 9;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Unary {
    Neg,
    Not,
    Floor,
    Ceil,
    Round,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Binary {
    Or,
    Xor,
    And,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Shl,
    Shr,
    Add,
    Sub,
    Mul,
    Div,
    Rem,
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Step {
    Num(f32),
    V,
    B(u8),
    Id(u16),
    Unary(Unary),
    Binary(Binary),
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CompileError {
    Lex,
    TooLong,
    Syntax,
    TooDeep,
}

#[derive(Clone, Copy)]
enum Pending {
    Group(Option<Unary>),
    Prefix(Unary),
    Infix(Binary),
}

impl Binary {
    fn precedence(self) -> u8 {
        match self {
            Binary::Or => 1,
            Binary::Xor => 2,
            Binary::And => 3,
            Binary::Eq | Binary::Ne => 4,
            Binary::Lt | Binary::Le | Binary::Gt | Binary::Ge => 5,
            Binary::Shl | Binary::Shr => 6,
            Binary::Add | Binary::Sub => 7,
            Binary::Mul | Binary::Div | Binary::Rem => 8,
        }
    }

    fn from_op(op: Op) -> Option<Self> {
        Some(match op {
            Op::Or => Binary::Or,
            Op::Xor => Binary::Xor,
            Op::And => Binary::And,
            Op::Eq => Binary::Eq,
            Op::Ne => Binary::Ne,
            Op::Lt => Binary::Lt,
            Op::Le => Binary::Le,
            Op::Gt => Binary::Gt,
            Op::Ge => Binary::Ge,
            Op::Shl => Binary::Shl,
            Op::Shr => Binary::Shr,
            Op::Plus => Binary::Add,
            Op::Minus => Binary::Sub,
            Op::Star => Binary::Mul,
            Op::Slash => Binary::Div,
            Op::Percent => Binary::Rem,
            _ => return None,
        })
    }
}

fn function_of(kind: FnKind) -> Unary {
    match kind {
        FnKind::Floor => Unary::Floor,
        FnKind::Ceil => Unary::Ceil,
        FnKind::Round => Unary::Round,
    }
}

fn reducible(pending: Pending, min_precedence: u8) -> Option<Step> {
    let (precedence, step) = match pending {
        Pending::Group(_) => return None,
        Pending::Prefix(u) => (PREFIX_PRECEDENCE, Step::Unary(u)),
        Pending::Infix(b) => (b.precedence(), Step::Binary(b)),
    };
    (precedence >= min_precedence).then_some(step)
}

struct Compiler<'a> {
    out: &'a mut [Step],
    len: usize,
    pending: [Pending; MAX_TOKENS],
    pending_len: usize,
    depth: usize,
    expect_operand: bool,
    awaiting_call: Option<Unary>,
}

impl<'a> Compiler<'a> {
    fn new(out: &'a mut [Step]) -> Self {
        Self {
            out,
            len: 0,
            pending: [Pending::Group(None); MAX_TOKENS],
            pending_len: 0,
            depth: 0,
            expect_operand: true,
            awaiting_call: None,
        }
    }

    fn emit(&mut self, step: Step) -> Result<(), CompileError> {
        let slot = self.out.get_mut(self.len).ok_or(CompileError::TooLong)?;
        *slot = step;
        self.len += 1;
        match step {
            Step::Unary(_) => {}
            Step::Binary(_) => self.depth = self.depth.saturating_sub(1),
            _ => self.depth += 1,
        }
        if self.depth > MAX_STACK_DEPTH {
            return Err(CompileError::TooDeep);
        }
        Ok(())
    }

    fn push(&mut self, pending: Pending) -> Result<(), CompileError> {
        let slot = self
            .pending
            .get_mut(self.pending_len)
            .ok_or(CompileError::TooLong)?;
        *slot = pending;
        self.pending_len += 1;
        Ok(())
    }

    fn pop(&mut self) -> Option<Pending> {
        self.pending_len = self.pending_len.checked_sub(1)?;
        Some(self.pending[self.pending_len])
    }

    fn reduce(&mut self, min_precedence: u8) -> Result<(), CompileError> {
        while let Some(step) = self
            .pending_len
            .checked_sub(1)
            .and_then(|top| reducible(self.pending[top], min_precedence))
        {
            self.pending_len -= 1;
            self.emit(step)?;
        }
        Ok(())
    }

    fn feed(&mut self, tok: TokKind) -> Result<(), CompileError> {
        if let Some(function) = self.awaiting_call.take() {
            return match tok {
                TokKind::Op(Op::LParen) => self.push(Pending::Group(Some(function))),
                _ => Err(CompileError::Syntax),
            };
        }
        if self.expect_operand {
            self.feed_operand(tok)
        } else {
            self.feed_operator(tok)
        }
    }

    fn feed_operand(&mut self, tok: TokKind) -> Result<(), CompileError> {
        match tok {
            TokKind::Num(n) => self.operand(Step::Num(n)),
            TokKind::V => self.operand(Step::V),
            TokKind::B(idx) => self.operand(Step::B(idx)),
            TokKind::Id(target_id) => self.operand(Step::Id(target_id)),
            TokKind::Fn(kind) => {
                self.awaiting_call = Some(function_of(kind));
                Ok(())
            }
            TokKind::Op(Op::LParen) => self.push(Pending::Group(None)),
            TokKind::Op(Op::Minus) => self.push(Pending::Prefix(Unary::Neg)),
            TokKind::Op(Op::Bang) => self.push(Pending::Prefix(Unary::Not)),
            TokKind::Op(Op::Plus) => Ok(()),
            TokKind::Op(_) => Err(CompileError::Syntax),
        }
    }

    fn operand(&mut self, step: Step) -> Result<(), CompileError> {
        self.emit(step)?;
        self.expect_operand = false;
        Ok(())
    }

    fn feed_operator(&mut self, tok: TokKind) -> Result<(), CompileError> {
        let TokKind::Op(op) = tok else {
            return Err(CompileError::Syntax);
        };
        if op == Op::RParen {
            return self.close_group();
        }
        let binary = Binary::from_op(op).ok_or(CompileError::Syntax)?;
        self.reduce(binary.precedence())?;
        self.push(Pending::Infix(binary))?;
        self.expect_operand = true;
        Ok(())
    }

    fn close_group(&mut self) -> Result<(), CompileError> {
        self.reduce(0)?;
        let Some(Pending::Group(call)) = self.pop() else {
            return Err(CompileError::Syntax);
        };
        call.map_or(Ok(()), |function| self.emit(Step::Unary(function)))
    }

    fn finish(mut self) -> Result<usize, CompileError> {
        if self.expect_operand || self.awaiting_call.is_some() {
            return Err(CompileError::Syntax);
        }
        self.reduce(0)?;
        if self.pending_len != 0 {
            return Err(CompileError::Syntax);
        }
        Ok(self.len)
    }
}

pub fn compile(expr: &[u8], out: &mut [Step]) -> Result<usize, CompileError> {
    let mut compiler = Compiler::new(out);
    for (index, tok) in Tokens::new(expr).enumerate() {
        if index >= MAX_TOKENS {
            return Err(CompileError::TooLong);
        }
        compiler.feed(tok.ok_or(CompileError::Lex)?)?;
    }
    compiler.finish()
}
