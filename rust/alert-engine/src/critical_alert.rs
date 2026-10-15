use crate::{crossed, AlertLevel};

#[must_use]
pub fn eval_critical_alert(value: f32, fires_at: f32, below: bool) -> AlertLevel {
    if crossed(value, fires_at, below) {
        AlertLevel::Critical
    } else {
        AlertLevel::Normal
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{eval_with_hysteresis, LevelHold};

    #[test]
    fn fires_at_and_past_a_rising_threshold() {
        assert_eq!(eval_critical_alert(979.0, 980.0, false), AlertLevel::Normal);
        assert_eq!(
            eval_critical_alert(980.0, 980.0, false),
            AlertLevel::Critical
        );
        assert_eq!(
            eval_critical_alert(1020.0, 980.0, false),
            AlertLevel::Critical
        );
    }

    #[test]
    fn fires_at_and_under_a_falling_threshold() {
        assert_eq!(eval_critical_alert(1.2, 1.0, true), AlertLevel::Normal);
        assert_eq!(eval_critical_alert(1.0, 1.0, true), AlertLevel::Critical);
        assert_eq!(eval_critical_alert(0.4, 1.0, true), AlertLevel::Critical);
    }

    #[test]
    fn never_fires_without_a_threshold() {
        assert_eq!(
            eval_critical_alert(5000.0, f32::NAN, false),
            AlertLevel::Normal
        );
    }

    #[test]
    fn holds_critical_inside_the_hysteresis_band() {
        let eval = |v: f32| eval_critical_alert(v, 1.0, true);
        let raised = eval_with_hysteresis(LevelHold::default(), 0.9, 0, 5.0, 0, eval);
        assert_eq!(raised.level, AlertLevel::Critical);
        let held = eval_with_hysteresis(raised, 1.03, 10, 5.0, 0, eval);
        assert_eq!(held.level, AlertLevel::Critical);
        let cleared = eval_with_hysteresis(held, 1.5, 20, 5.0, 0, eval);
        assert_eq!(cleared.level, AlertLevel::Normal);
    }
}
