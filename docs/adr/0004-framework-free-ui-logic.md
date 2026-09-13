# ADR-0004: Framework-free UI logic, egui quarantined to the shell

## Status
Accepted

## Context
UI frameworks (egui/eframe here) change fast and are expensive to test; business
rules and view models are stable and cheap to test. Mixing them makes tests
need a display and rewrites need a rewrite.

## Decision
All UI state and transitions live in `crates/desktop/src/app.rs` as plain Rust
structs (`AppState` and its state machine, `SaveRequest`) over the
framework-free view models from the `crates/adapters/presentation` adapter
(`EditorState`, list items, presenter mapping), unit-tested headless. egui
appears only in `crates/desktop/src/main.rs` and the render callbacks under
`crates/desktop/src/views/`, which render that state and forward events to use
cases. `eframe::run_native` is never executed by tests.

## Consequences
- The three-pane behavior (selection, search mode, confirm dialogs, save flow)
  is fully tested without a window server (the shell state machine in
  `crates/desktop/src/app.rs` and its tests, plus `crates/desktop/tests/`).
- Rendering code is admittedly thin-and-manual; richer widgets may need more
  glue later — acceptable at this scale.
- Porting to another toolkit means rewriting `main.rs` and the `views/`
  callbacks only — `crates/adapters/presentation` view models and the shell
  logic in `app.rs` do not move.

## What this teaches
Keep the framework at the edge: test the *behavior* of your UI, not its widgets.
