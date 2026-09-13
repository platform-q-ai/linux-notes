# ADR-0003: Explicit save (v1), autosave-compatible state model

## Status
Accepted

## Context
Notes apps live or die by not losing input. Fully automatic saves can persist
half-typed titles and fight the user; purely manual models risk silent loss.

## Decision
v1 uses explicit Save: every edit flips a `dirty` flag in the editor state, the
button is enabled only while dirty, and a successful save clears it and refreshes
the list. The state model (`EditorState.dirty`, `save_requested()`) was designed
so an autosave debounce can be added later *without changing the ports or use
cases* — autosave would simply call the same `UpdateNote` on a timer.

## Consequences
- Predictable, understandable behavior for v1 (per product guidance).
- No partially-written titles in the database; domain invariants run at save.
- Cost: users must press Save (mitigated by the visible “(unsaved)” marker).

## What this teaches
Model the *intent to persist* as data (`dirty`) instead of burying it in event
handlers; then "manual vs autosave" becomes a policy choice, not a redesign.
