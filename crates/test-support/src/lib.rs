//! Shared test support for rusty-notes: behavioral contracts and in-memory fakes.
//!
//! Test-only by design (issue #2, F8/F12): this crate must appear ONLY in
//! `[dev-dependencies]` of the crates that use it — never in any production
//! dependency graph. It exists so adapter suites can hold their implementations
//! to the same behavioral contract the in-memory fakes pass.
//!
//! - `fakes/` — in-memory port implementations (repository, search, clock, ids).
//! - `contracts/` — shared suites run by every adapter's tests via `run_all` /
//!   `run_all_search`.

pub mod contracts;
pub mod fakes;
