use can_parser::expr::{eval, eval_checked, EvalContext, RefValue};
use can_parser::expr_compile::{compile, CompileError, Step, MAX_STACK_DEPTH};

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
    assert_eq!(
        eval(b"(Floor(V/200)/2)*100", &ctx_no_refs(401.0, &[])),
        100.0
    );
}

#[test]
fn catalogue_patterns() {
    assert_eq!(
        eval(b"(V==0xD7)|(V==0xEF)", &ctx_no_refs(0xD7 as f32, &[])),
        1.0
    );
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

fn compile_result(expr: &str) -> Result<usize, CompileError> {
    let mut steps = [Step::V; 64];
    compile(expr.as_bytes(), &mut steps)
}

fn right_nested_sum(levels: usize) -> String {
    let mut expr = String::from("1");
    for _ in 1..levels {
        expr = format!("1+({expr})");
    }
    expr
}

#[test]
fn deep_parentheses_compile_without_recursion() {
    let expr = format!("{}V{}", "(".repeat(31), ")".repeat(31));
    assert_eq!(eval(expr.as_bytes(), &ctx_no_refs(5.0, &[])), 5.0);
    let negations = format!("{}V", "-".repeat(62));
    assert_eq!(eval(negations.as_bytes(), &ctx_no_refs(5.0, &[])), 5.0);
}

#[test]
fn operand_stack_deeper_than_the_cap_is_rejected() {
    let at_cap = right_nested_sum(MAX_STACK_DEPTH);
    assert!(compile_result(&at_cap).is_ok());
    assert_eq!(
        eval(at_cap.as_bytes(), &ctx_no_refs(0.0, &[])),
        MAX_STACK_DEPTH as f32
    );
    let over_cap = right_nested_sum(MAX_STACK_DEPTH + 1);
    assert_eq!(compile_result(&over_cap), Err(CompileError::TooDeep));
    assert_eq!(eval(over_cap.as_bytes(), &ctx_no_refs(0.0, &[])), 0.0);
    assert!(eval_checked(over_cap.as_bytes(), &ctx_no_refs(0.0, &[])).is_nan());
}

#[test]
fn more_than_sixty_four_tokens_is_too_long() {
    let expr = vec!["1"; 33].join("+");
    assert_eq!(compile_result(&expr), Err(CompileError::TooLong));
}

#[test]
fn precedence_and_associativity() {
    let c = ctx_no_refs(3.0, &[]);
    assert_eq!(eval(b"10-4-3", &c), 3.0);
    assert_eq!(eval(b"64/4/2", &c), 8.0);
    assert_eq!(eval(b"-V*2", &c), -6.0);
    assert_eq!(eval(b"2*-V", &c), -6.0);
    assert_eq!(eval(b"!V==0", &c), 1.0);
    assert_eq!(eval(b"1+2<<1", &c), 6.0);
    assert_eq!(eval(b"1|2^3&1", &c), 3.0);
    assert_eq!(eval(b"V<4==1", &c), 1.0);
    assert_eq!(eval(b"+V", &c), 3.0);
    assert_eq!(eval(b"--V", &c), 3.0);
    assert_eq!(eval(b"Round(-2.5)", &c), -3.0);
}

#[test]
fn malformed_expressions_are_syntax_errors() {
    for expr in [
        "V V", "()", "(V", "V)", "Floor V", "Floor()", "V(1)", "1,2", "V+", "*V", " ",
    ] {
        assert_eq!(compile_result(expr), Err(CompileError::Syntax), "{expr}");
    }
}
