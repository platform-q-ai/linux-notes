//! [`FixedClock`]: deterministic [`Clock`] fake. Test-only: see crate `lib.rs`.

use std::sync::Mutex;

use rusty_notes_application::ports::Clock;
use rusty_notes_domain::Timestamp;

/// Deterministic [`Clock`] for tests.
pub struct FixedClock {
    now: Mutex<Timestamp>,
}

impl FixedClock {
    pub fn new(now: Timestamp) -> Self {
        Self {
            now: Mutex::new(now),
        }
    }

    /// Advances the clock (used to test `updated_at` ordering).
    pub fn advance(&self, next: Timestamp) {
        *self.now.lock().expect("clock lock") = next;
    }
}

impl Clock for FixedClock {
    fn now(&self) -> Timestamp {
        self.now.lock().expect("clock lock").clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_clock_holds_and_advances() {
        let clock = FixedClock::new(Timestamp("t0".into()));
        assert_eq!(clock.now().0, "t0");
        clock.advance(Timestamp("t1".into()));
        assert_eq!(clock.now().0, "t1");
    }
}
