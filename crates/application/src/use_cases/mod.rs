//! Use cases: plain structs over the ports. No framework, no IO, no async.
//!
//! One snake_case module per use case (issue #2, F3), grouped by aggregate:
//! `notes/` and `folders/`.

pub mod folders;
pub mod notes;
