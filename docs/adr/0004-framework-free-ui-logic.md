# ADR-0004: Framework-free UI logic, egui quarantined to the shell

## Status
Accepted

## Context
UI frameworks (egui/eframe here) change fast and are expensive to test; business
rules and view models are stable and cheap to test. Mixing them makes tests
need a display and rewrites need a rewrite.

## Decision
All UI state and transitions live in `app/desktop/src/ui.rs` as plain Rust
structs (`AppState`, `SaveRequest`, `PendingConfirm`) over application presenter
types, unit-tested headless. egui appears only in `main.rs`, which renders that
state and forwards events to use cases. `eframe::run_native` is never executed
by tests.

## Consequences
- The three-pane behavior (selection, search mode, confirm dialogs, save flow)
  is fully tested without a window server.
- Rendering code is admittedly thin-and-manual; richer widgets may need more
  glue later — acceptable at this scale.
- Porting to another toolkit means rewriting `main.rs` only.

## What this teaches
Keep the framework at the edge: test the *behavior* of your UI, not its widgets.
