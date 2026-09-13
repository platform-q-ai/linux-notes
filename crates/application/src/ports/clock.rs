//! Time port.

use rusty_notes_domain::Timestamp;

/// Time port: use cases take timestamps as injected values, keeping the domain
/// deterministic and testable. Production implementation:
/// `rusty_notes_system::SystemClock`. Tests use `test-support`'s `FixedClock`
/// (dev-dependency only — it never ships in the production graph).
pub trait Clock: Send + Sync {
    fn now(&self) -> Timestamp;
}
