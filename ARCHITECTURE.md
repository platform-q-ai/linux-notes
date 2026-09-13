# Architecture

rusty-notes is deliberately small, but it is structured the way a larger app
should be: dependencies point inward, the domain knows nothing, and every
backend is substitutable behind a narrow port.

```
┌───────────────────── crates/desktop (eframe/egui) ──────────────────────────┐
│  composition root (composition.rs): builds real adapters (SQLite repos +    │
│  search, SystemClock, UniqueIds from adapters/system), injects ports, and   │
│  drives the headless shell state machine (app.rs) rendered by views/        │
└──────────────▲───────────────────────────────────────────┬─────────────────┘
               │ presenter view models                     │ use-case calls
┌──────────────┴───────────────────────────────────────────▼─────────────────┐
│                  crates/application (ports + use cases)                    │
│  ports/: NoteRepository / SearchService / Clock / IdGenerator ← owned HERE  │
│  use_cases/: one snake_case module per use case (notes/, folders/)          │
└──────────────▲───────────────────────────────────────────┬─────────────────┘
               │ implements ports                          │ entity ops
┌──────────────┴────────────────────┐   ┌────────────────────▼────────────────┐
│   crates/adapters/sqlite          │   │        crates/domain (pure)         │
│  connection, migrations, soft     │──▶│ notes/ (Note, NoteId, NoteDraft),   │
│  delete, FTS5 search, error       │   │ folders/ (Folder, FolderId),        │
│  mapping — one responsibility per │   │ shared/timestamp, error.rs          │
│  module                           │   │ invariants: non-empty title, 1 MiB  │
└───────────────────────────────────┘   │ body cap on every mutation path     │
                                        └─────────────────────────────────────┘
      Side crates: crates/adapters/presentation (framework-free view models),
      crates/adapters/system (SystemClock, UniqueIds — production Clock/Ids),
      crates/test-support (fakes + shared contract suites, dev-dependency only).
```

## Rules (enforced by compilation and `scripts/arch-check.py` in CI)

1. **`crates/domain` depends on nothing.** No IO, no crates, no async.
   Invariants are enforced in constructors and mutators (`Note::new` and
   `Note::edit_body` both reject bodies over `MAX_BODY_BYTES`, so the body cap
   holds on every mutation path; `Note::new` and `Note::rename` reject empty
   titles).
2. **`crates/application` depends only on `domain`.** The ports live here — the
   app owns its interface to the world, not the adapter. One port per module
   under `ports/`; one use case per module under `use_cases/`. Test fakes and
   contract suites are *not* here: they live in `crates/test-support`, which
   the application crate does not depend on in any capacity.
3. **`crates/adapters/*` depend inward on `application`/`domain`.**
   `adapters/sqlite` implements the repository and search ports (and is the
   only crate that knows SQL exists); `adapters/presentation` is a
   framework-free adapter crate — view models and presenter mapping, no egui,
   no ports; `adapters/system` provides the production `SystemClock` and
   restart-stable `UniqueIds`, which the composition root (`desktop`'s
   `composition.rs`) wires so production identity never comes from
   test-support fakes.
4. **`crates/desktop` is the only egui/eframe crate** and the only place that
   may call all lower layers. `eframe::run_native` appears exactly once (in
   `main.rs`). Framework-free shell state (`app.rs`) is unit-tested headless;
   `views/` are thin render callbacks.
5. **`crates/test-support` is dev-dependency only.** It appears in no
   production dependency graph — `scripts/arch-check.py` fails the build if it
   ever does — and adapter integration tests, not the application crate, run
   its shared suites (avoiding an unresolvable application ↔ test-support
   dev-dependency cycle).
6. No async runtime anywhere: one desktop, one SQLite connection, no network.

## Why the contract suite matters

The behavioral contract of the ports is defined once in
`crates/test-support/src/contracts/` (`repository_contract.rs`,
`search_contract.rs`, exposed as `run_all` / `run_all_search`). The in-memory
fakes in `test-support/src/fakes/` pass it, and the SQLite adapter passes the
*same* cases in its integration tests
(`crates/adapters/sqlite/tests/repository_contract.rs`, `search_contract.rs`).
Consequences:

- The fake is not a toy that diverges from reality — it is held to the same
  contract as the production adapter (LSP, tested not inherited).
- Use-case tests run against the fake in microseconds; SQLite-specific behavior
  (migrations, triggers, escaping) is tested in the adapter crate.
- A future adapter (Postgres, web) proves substitutability by running the same
  suite — the definition of “pluggable” here is *behavioral*, not structural.
- The suites live in `test-support` (a dev-dependency-only crate) and are
  consumed from adapter integration tests; keeping them inside `application`
  would make it depend on test fakes in production, and a direct
  application ↔ test-support dev-dependency cycle cannot even be resolved by
  cargo.

## The three data flows

- **List**: use case `ListNotes` → port → SQLite `SELECT … ORDER BY updated_at
  DESC, id DESC` → `adapters/presentation` presenter rows → middle pane.
- **Edit (explicit save)**: editor mutates `EditorState` (dirty flag) →
  **Save** → `UpdateNote` → domain `rename`/`edit_body` (invariants +
  timestamp; both reject, so the body cap holds on every mutation path) → port
  `update_note` → SQLite UPDATE → FTS trigger syncs the index → refresh.
- **Search**: search box → `SearchNotes` → port `search()` → FTS5 `MATCH` with
  quoted tokens and bm25 column weights (title > body) → resolved to notes via
  the repository → `adapters/presentation` presenter rows.

## Deleted notes and folders

Soft delete is a persistence strategy behind the port; the domain model never
carries a `deleted` flag — listings and search simply exclude deleted rows.
Deletion is not lossy: the `NoteRepository` port exposes
`get_note_including_deleted` (recovery surface) and `purge_note` (the “empty
trash” surface that finally removes a deleted note), and `delete_folder`
refuses to delete a folder that still has non-deleted notes (purge the deleted
notes first) so a folder holding deleted notes is never silently destroyed.

## Teaching points (what to copy to the next project)

- Newtype ids everywhere (`NoteId`, `FolderId`) — stringly-typed bugs die here.
- Ports are *owned by the consumer*, not the provider: SQLite implements a trait
  it did not design, so the trait stays minimal and need-based.
- Time and identity are ports (`Clock`, `Ids` — see `ports/clock.rs` and
  `ports/id_generator.rs`) — that is why every test
  is deterministic and no test sleeps. Production implementations live in
  `crates/adapters/system` (`SystemClock`, `UniqueIds`), never in test-support.
- Soft delete is a persistence strategy behind the port; the domain model never
  carries a `deleted` flag — listings and search simply exclude it, and the
  port exposes recovery (`get_note_including_deleted`) and purge (`purge_note`)
  surfaces.
- Framework code (egui) is quarantined in one crate (`crates/desktop`) so the
  app logic could move to another UI toolkit without touching domain,
  application, or adapters.
