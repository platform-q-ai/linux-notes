//! rusty-notes system adapters: the production implementations of the [`Clock`]
//! and [`Ids`] ports from the application layer.
//!
//! [`SystemClock`] reads the operating-system wall clock; [`UniqueIds`] hands out
//! identifiers that stay unique across app restarts. Both live here — not in
//! test-support — because the composition root must wire production adapters
//! (issue #2: production clock/ID adapters must not come from test-support).
//!
//! [`Clock`]: rusty_notes_application::ports::Clock
//! [`Ids`]: rusty_notes_application::ports::Ids

pub mod system_clock;
pub mod unique_ids;

pub use system_clock::SystemClock;
pub use unique_ids::UniqueIds;
