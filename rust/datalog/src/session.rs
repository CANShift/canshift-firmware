pub const DATALOG_SESSION_IDLE: u8 = 0;
pub const DATALOG_SESSION_OPEN: u8 = 1;
pub const DATALOG_SESSION_HALTED: u8 = 2;

pub const DATALOG_ACTION_NONE: u8 = 0;
pub const DATALOG_ACTION_OPEN: u8 = 1;
pub const DATALOG_ACTION_CLOSE: u8 = 2;

pub const DATALOG_CLOSE_AFTER_SILENT_S: u32 = 10;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct SessionInputs {
    pub bus_silent: bool,
    pub card_ready: bool,
    pub workshop: bool,
    pub reserved: u8,
    pub silent_seconds: u32,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Session {
    pub state: u8,
}

fn wants_close(inputs: &SessionInputs) -> bool {
    inputs.workshop || !inputs.card_ready || inputs.silent_seconds >= DATALOG_CLOSE_AFTER_SILENT_S
}

fn wants_open(inputs: &SessionInputs) -> bool {
    inputs.card_ready && !inputs.bus_silent && !inputs.workshop
}

pub fn step(session: &mut Session, inputs: &SessionInputs) -> u8 {
    match session.state {
        DATALOG_SESSION_OPEN if wants_close(inputs) => {
            session.state = DATALOG_SESSION_IDLE;
            DATALOG_ACTION_CLOSE
        }
        DATALOG_SESSION_IDLE if wants_open(inputs) => {
            session.state = DATALOG_SESSION_OPEN;
            DATALOG_ACTION_OPEN
        }
        DATALOG_SESSION_HALTED if inputs.bus_silent => {
            session.state = DATALOG_SESSION_IDLE;
            DATALOG_ACTION_NONE
        }
        _ => DATALOG_ACTION_NONE,
    }
}

pub fn halt(session: &mut Session) {
    session.state = DATALOG_SESSION_HALTED;
}

#[cfg(test)]
mod tests;
