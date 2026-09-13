# rusty-notes

A local-first, Apple Notes–inspired desktop notes app in Rust. Three panes —
folders, note list with search, editor — backed by SQLite with full-text search.

**Status: v1 feature branch** (`feat/basic-notes`). Plain-text notes, folders,
search, and reliable explicit save.

## Quick start

```sh
# 1. Format check
cargo fmt --all -- --check

# 2. Lints + tests (workspace, all targets, warnings denied)
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace

# 3. Run the desktop app
cargo run -p rusty-notes-desktop
```

The app stores everything in `rusty-notes.db` (SQLite) in the current working
directory. Creating a note or folder writes it immediately; the editor uses an
explicit **Save** button (unsaved changes are marked “(unsaved)”).

## Layout

| Crate | Role |
|---|---|
| `domain/` | Entities and invariants (`Note`, `Folder`, newtype ids). Pure Rust. |
| `application/` | Ports (`NoteRepository`, `SearchService`, `Clock`, `Ids`), use cases, in-memory adapters, shared contract suite, presenter types. Depends only on `domain`. |
| `infrastructure/sqlite/` | The real adapter: migrations, soft delete, FTS5 search. Depends on `application` ports. |
| `app/desktop/` | The only egui/eframe crate: composition root + three-pane shell. |

See [ARCHITECTURE.md](ARCHITECTURE.md) for the dependency rules and
`docs/adr/` for the decisions behind them.

## Testing approach

The application layer defines a **shared contract suite**
(`application/src/contract.rs`). Both the in-memory fake and the SQLite adapter
run the very same behavioral cases (`application/tests/memory_contract.rs`,
`infrastructure/sqlite/tests/sqlite_contract.rs`), which is what makes the fake
trustworthy for fast tests and the adapter substitutable in production.
