use super::*;

fn live() -> SessionInputs {
    SessionInputs {
        bus_silent: false,
        card_ready: true,
        workshop: false,
        reserved: 0,
        silent_seconds: 0,
    }
}

fn silent_for(seconds: u32) -> SessionInputs {
    SessionInputs {
        bus_silent: true,
        silent_seconds: seconds,
        ..live()
    }
}

fn open_session() -> Session {
    let mut session = Session::default();
    assert_eq!(step(&mut session, &live()), DATALOG_ACTION_OPEN);
    session
}

#[test]
fn a_live_bus_with_a_card_opens_a_session_once() {
    let mut session = open_session();
    assert_eq!(session.state, DATALOG_SESSION_OPEN);
    assert_eq!(step(&mut session, &live()), DATALOG_ACTION_NONE);
}

#[test]
fn nothing_opens_without_a_card_or_in_workshop_mode() {
    let mut session = Session::default();
    let no_card = SessionInputs {
        card_ready: false,
        ..live()
    };
    let workshop = SessionInputs {
        workshop: true,
        ..live()
    };
    assert_eq!(step(&mut session, &no_card), DATALOG_ACTION_NONE);
    assert_eq!(step(&mut session, &workshop), DATALOG_ACTION_NONE);
    assert_eq!(step(&mut session, &silent_for(0)), DATALOG_ACTION_NONE);
}

#[test]
fn a_short_silence_keeps_the_session_open() {
    let mut session = open_session();
    let almost = silent_for(DATALOG_CLOSE_AFTER_SILENT_S - 1);
    assert_eq!(step(&mut session, &almost), DATALOG_ACTION_NONE);
    let enough = silent_for(DATALOG_CLOSE_AFTER_SILENT_S);
    assert_eq!(step(&mut session, &enough), DATALOG_ACTION_CLOSE);
    assert_eq!(session.state, DATALOG_SESSION_IDLE);
}

#[test]
fn entering_workshop_mode_closes_the_session() {
    let mut session = open_session();
    let workshop = SessionInputs {
        workshop: true,
        ..live()
    };
    assert_eq!(step(&mut session, &workshop), DATALOG_ACTION_CLOSE);
}

#[test]
fn a_halted_session_waits_for_the_bus_to_go_quiet_before_retrying() {
    let mut session = open_session();
    halt(&mut session);
    assert_eq!(step(&mut session, &live()), DATALOG_ACTION_NONE);
    assert_eq!(step(&mut session, &silent_for(1)), DATALOG_ACTION_NONE);
    assert_eq!(session.state, DATALOG_SESSION_IDLE);
    assert_eq!(step(&mut session, &live()), DATALOG_ACTION_OPEN);
}
