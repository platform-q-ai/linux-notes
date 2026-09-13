# ADR-0002: SQLite with embedded versioned migrations

## Status
Accepted

## Context
The app must be local-first and durable with zero setup: no server, no daemon.
Search must rank results and stay consistent with edits. Schemas evolve.

## Decision
rusqlite (bundled SQLite, FTS5 enabled) with the database file created next to
the app (`crates/desktop`'s composition root passes `rusty-notes.db` in the
current directory to `crates/adapters/sqlite`'s `connection::open`). Migrations
are embedded in the adapter (`migrations.rs`) and keyed by `PRAGMA
user_version` (v1 = folders/notes/soft delete/FTS5 + sync triggers). A canary
query at open time (`connection::fts5_canary`) fails fast if FTS5 is
unavailable. Every connection sets `PRAGMA foreign_keys=ON`.

## Consequences
- Reopen safety and forward migrations are first-class, tested behaviors.
- FTS5 external-content tables + triggers keep index and rows consistent
  (verified by tests), at the cost of slightly more schema code.
- Bundling SQLite trades binary size for "works on any machine".

## What this teaches
Treat schema like code: version it, migrate it explicitly, and test reopen —
durability is a feature, not an accident.
