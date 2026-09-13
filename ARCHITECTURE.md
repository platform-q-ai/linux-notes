# Architecture

rusty-notes is deliberately small, but it is structured the way a larger app
should be: dependencies point inward, the domain knows nothing, and every
backend is substitutable behind a narrow port.

```
┌────────────────────────── app/desktop (eframe/egui) ──────────────────────────┐
│  composition root (main.rs): builds adapters, injects ports, renders panes    │
└──────────────▲───────────────────────────────────────────┬──────────────────┘
               │ presenter view models                     │ use-case calls
┌──────────────┴───────────────────────────────────────────▼──────────────────┐
│                        application (ports + use cases)                      │
│   NoteRepository / SearchService / Clock / Ids   ← narrow, owned HERE        │
│   in-memory adapters + shared contract suite + presenter                     │
└──────────────▲───────────────────────────────────────────┬──────────────────┘
               │ implements ports                          │ entity ops
┌──────────────┴────────────────────┐   ┌────────────────────▼─────────────────┐
│      infrastructure/sqlite        │   │            domain (pure)             │
│  migrations, soft delete, FTS5    │──▶│  Note / Folder / invariants / errors │
└───────────────────────────────────┘   └──────────────────────────────────────┘
```

## Rules (enforced by compilation)

1. **`domain` depends on nothing.** No IO, no crates, no async. Invariants are
   enforced in constructors (`Note::new` rejects empty titles; bodies are capped).
2. **`application` depends only on `domain`.** The ports live here — the app owns
   its interface to the world, not the adapter.
3. **`infrastructure/sqlite` depends on `application`** (to implement the ports)
   and on `rusqlite`. It is the only crate that knows SQL exists.
4. **`app/desktop` is the only egui/eframe crate** and the only place that may
   call all four lower layers. `eframe::run_native` appears exactly once.
5. No async runtime anywhere: one desktop, one SQLite connection, no network.

## Why the contract suite matters

`application/src/contract.rs` defines the behavioral contract of the ports as
generic test cases. The in-memory fake passes them (`memory_contract.rs`), and
the SQLite adapter passes the *same* cases (`sqlite_contract.rs`). Consequences:

- The fake is not a toy that diverges from reality — it is held to the same
  contract as the production adapter (LSP, tested not inherited).
- Use-case tests run against the fake in microseconds; SQLite-specific behavior
  (migrations, triggers, escaping) is tested in the adapter crate.
- A future adapter (Postgres, web) proves substitutability by running the same
  suite — the definition of “pluggable” here is *behavioral*, not structural.

## The three data flows

- **List**: use case `ListNotes` → port → SQLite `SELECT … ORDER BY updated_at
  DESC, id DESC` → presenter rows → middle pane.
- **Edit (explicit save)**: editor mutates `EditorState` (dirty flag) →
  **Save** → `UpdateNote` → domain `rename`/`edit_body` (invariants + timestamp)
  → port `update_note` → SQLite UPDATE → FTS trigger syncs the index → refresh.
- **Search**: search box → `SearchNotes` → port `search()` → FTS5 `MATCH` with
  quoted tokens and bm25 column weights (title > body) → resolved to notes via
  the repository → presenter rows.

## Teaching points (what to copy to the next project)

- Newtype ids everywhere (`NoteId`, `FolderId`) — stringly-typed bugs die here.
- Ports are *owned by the consumer*, not the provider: SQLite implements a trait
  it did not design, so the trait stays minimal and need-based.
- Time and identity are ports (`Clock`, `Ids`) — that is why every test is
  deterministic and no test sleeps.
- Soft delete is a persistence strategy behind the port; the domain model never
  carries a `deleted` flag — listings and search simply exclude it.
- Framework code (egui) is quarantined in one crate so the app logic could move
  to another UI toolkit without touching domain, application, or adapter.
