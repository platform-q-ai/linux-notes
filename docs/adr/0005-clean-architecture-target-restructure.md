# ADR-0005: Clean Architecture target restructure into `crates/` (issue #2)

## Status
Accepted (implemented by the fix round for PR #1; supersedes the directory
layout implied by ADR-0004 and the crate table that ADR-0001's decision relied on)

## Context
The original PR used four top-level directories — `domain/`, `application/`,
`infrastructure/sqlite/`, `app/desktop/` — with several responsibilities
aggregated per crate: all four ports in one `ports.rs`, all ten use cases in
one `use_cases.rs`, the entire SQLite adapter in one `lib.rs`, UI editor state
inside `application`, test fakes and the shared contract suite compiled into
the production application library, and a test-only `SequentialIds` fake wired
as the production ID generator. Review of PR #1 (findings F1–F12) blocked
merge on this: an inward-pointing dependency graph alone does not satisfy the
project's Clean Architecture acceptance requirement
([issue #2](https://github.com/platform-q-ai/rusty-notes/issues/2),
[wiki: Clean-Architecture-Target](https://github.com/platform-q-ai/rusty-notes/wiki/Clean-Architecture-Target))
— names and nesting must make responsibilities discoverable, and test-only
code must never be part of the production build.

## Decision
Restructure the workspace into a `crates/` tree (full target shape, module
naming, and rules in `docs/architecture-target.md`):

- `crates/domain` — entities and invariants, split into `notes/`, `folders/`,
  `shared/`, `error.rs`; no in-workspace dependencies.
- `crates/application` — `ports/` (one trait per module) and `use_cases/`
  (one snake_case module per use case under `notes/`/`folders/`); depends only
  on `domain`.
- `crates/adapters/{sqlite,presentation,system}` — the SQLite adapter split
  into `connection`/`migrations`/`note_repository`/`search_service`/
  `error_mapping`; UI view models moved *out of* `application` into the
  framework-free `presentation` adapter; production `Clock`/`Ids`
  implementations (`SystemClock`, `UniqueIds`) in `system`.
- `crates/desktop` — the only egui/eframe crate, split into a composition root
  (`composition.rs`), a headless shell state machine (`app.rs`), and thin
  render callbacks (`views/`, including the dismissible error banner).
- `crates/test-support` — the behavioral contract suites and in-memory fakes,
  extracted from `application` into a separate crate used **via
  dev-dependencies only**. Cross-crate contract suites are run from adapter
  integration tests, never through an application ↔ test-support dev-dependency
  (which cargo could not resolve).
- The composition root wires `SystemClock` and restart-stable `UniqueIds`
  from `adapters/system` — production identity never comes from test-support
  fakes.

Deviation note: issue #2 allows system adapters to be "grouped pragmatically
with composition if a separate crate is needless" — we chose the separate
`crates/adapters/system` crate (no deviation), because the production/restart
ID semantics are exactly what needed a non-fake home and a name future
engineers can find.

The old `domain/`, `application/`, `infrastructure/`, and `app/` directories
were deleted in this restructure; the workspace builds solely from `crates/`.
Enforcement is automated: CI runs
`scripts/arch-check.py` (inward-only edges, no test-support in the production
graph, no application ↔ test-support dev-dependency cycle) plus a negative
check proving the check fails on a deliberately corrupted dependency graph —
see `docs/architecture-target.md` and the `architecture-check` CI job.

## Consequences
- Every responsibility has a discoverable, single home; lib.rs/mod.rs files
  re-export rather than aggregate (review findings F1–F8 addressed structurally).
- The production dependency graph excludes test-only code by construction, and
  CI fails loudly if that regresses (F9), including on a deliberate violation.
- Docs (README, ARCHITECTURE.md, ADRs) must track the real tree; they were
  updated in the same fix round (F10) so they no longer enshrine the old
  layout as intended design.
- Behavior is unchanged: the eight validated behavior findings get regression
  tests in the same fix round, and all existing tests stay green.

## What this teaches
Make the architecture load-bearing: when the target shape, the code, the
automated checks, and the docs all agree, a boundary regression is a failed CI
run instead of a slow architectural erosion.
