# ADR-0001: Ports owned by the application layer (hexagonal, compile-time)

## Status
Accepted

## Context
SQLite, the file system, the clock, and the UI all change for different reasons.
If the use cases called rusqlite directly, unit tests would need either a real
database or a mock framework, and swapping any backend would ripple upward.

## Decision
The application crate defines `NoteRepository`, `SearchService`, `Clock`, and
`Ids` as plain Rust traits and depends only on them. Adapters (in-memory,
SQLite) implement these traits; the composition root injects them. All edges are
compile-time generics/`Arc<dyn Trait>` — no DI framework, no reflection.

## Consequences
- Use-case tests are pure and instant; the fake and the real adapter are held to
  the same behavioral contract suite.
- Adding a backend touches only the new adapter and the composition root.
- Cost: a small amount of delegation code; avoided trait theater by having
  exactly one reason for each port to exist.

## What this teaches
Dependency inversion is about *who owns the interface*: the consumer designs the
narrow port it needs; the adapter conforms.
