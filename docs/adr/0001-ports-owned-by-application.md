# ADR-0001: Ports owned by the application layer (hexagonal, compile-time)

## Status
Accepted

## Context
SQLite, the file system, the clock, and the UI all change for different reasons.
If the use cases called rusqlite directly, unit tests would need either a real
database or a mock framework, and swapping any backend would ripple upward.

## Decision
The ports (`NoteRepository`, `SearchService`, `Clock`, `Ids`) live in
`crates/application/src/ports/` — one trait per module — and the application
crate depends only on them and `domain`. Adapters implement these traits and
are wired by the composition root: `crates/adapters/sqlite` (repository +
search), `crates/adapters/system` (`SystemClock`, `UniqueIds`), and the
in-memory fakes in `crates/test-support` (dev-dependency only). All edges are
compile-time generics/`Arc<dyn Trait>` — no DI framework, no reflection.
Dependency arrows are enforced mechanically by `scripts/arch-check.py` in CI
(see `docs/architecture-target.md`).

## Consequences
- Use-case tests are pure and instant; the fake and the real adapter are held to
  the same behavioral contract suite (located in `crates/test-support/src/contracts/`,
  run by adapter integration tests, since a direct application ↔ test-support
  dev-dependency would be a cargo-unresolvable cycle).
- Adding a backend touches only the new adapter and the composition root.
- Cost: a small amount of delegation code; avoided trait theater by having
  exactly one reason for each port to exist.

## What this teaches
Dependency inversion is about *who owns the interface*: the consumer designs the
narrow port it needs; the adapter conforms.
