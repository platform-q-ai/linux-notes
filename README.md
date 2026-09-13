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

# 3. Architecture/dependency-boundary check
python3 scripts/arch-check.py

# 4. Run the desktop app
cargo run -p rusty-notes-desktop
```

The app stores everything in `rusty-notes.db` (SQLite) in the current working
directory. Creating a note or folder writes it immediately; the editor uses an
explicit **Save** button (unsaved changes are marked “(unsaved)”).

## Layout

The workspace lives under `crates/` and follows the
[Clean Architecture target](docs/architecture-target.md)
([issue #2](https://github.com/platform-q-ai/rusty-notes/issues/2)): dependencies
point strictly inward — `domain` ← `application` ← `adapters/*` ← `desktop` —
and `scripts/arch-check.py` enforces that in CI.

| Crate | Role |
|---|---|
| `crates/domain` | Entities and invariants: `notes/` (`Note`, `NoteId`, `NoteDraft`), `folders/` (`Folder`, `FolderId`), `shared/timestamp`, `error.rs`. Pure Rust, no in-workspace dependencies. |
| `crates/application` | Ports (`note_repository`, `search_service`, `clock`, `id_generator` — one trait per file under `ports/`) and one snake_case module per use case under `use_cases/notes/` and `use_cases/folders/`. Depends only on `domain`. |
| `crates/adapters/sqlite` | The real adapter: connection handling, embedded migrations, soft delete, FTS5 search, error mapping. Depends on `application` ports. |
| `crates/adapters/presentation` | Framework-free view models for the desktop shell (`EditorState`, list items, presenter functions) — explicitly presentation, not application policy. |
| `crates/adapters/system` | Production `Clock` and `Ids` adapters (`SystemClock`, `UniqueIds`) wired by the composition root. |
| `crates/desktop` | The only egui/eframe crate: composition root (`composition.rs`), shell state machine (`app.rs`), and the three-pane rendering in `views/`. |
| `crates/test-support` | Shared behavioral contract suites and in-memory fakes. Dev-dependency **only** — it never appears in any production dependency graph. |

The original `domain/`, `application/`, `infrastructure/`, and `app/`
directories at the repository root were removed in the restructure (ADR-0005);
the workspace builds solely from `crates/`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the dependency rules and
`docs/adr/` for the decisions behind them.

## Testing approach

The shared behavioral contract suites live in the **`crates/test-support`**
crate (`test-support/src/contracts/`: `run_all` / `run_all_search`), together
with the in-memory fakes they were written against
(`test-support/src/fakes/`). `test-support` is a dev-dependency only: the
SQLite adapter's integration tests
(`crates/adapters/sqlite/tests/repository_contract.rs`,
`search_contract.rs`, `persistence.rs`) and the desktop behavior tests
(`crates/desktop/tests/`) run the very same behavioral cases the in-memory
fakes pass, which is what makes the fake trustworthy for fast tests and the
adapter substitutable in production. The application crate itself does not
depend on test-support (that would be an unresolvable dev-dependency cycle);
cross-crate suites are located in adapter integration tests instead.
