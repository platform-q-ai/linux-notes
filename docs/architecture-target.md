# Clean Architecture target for rusty-notes

This is the project's required Clean Architecture target, as specified by
[issue #2](https://github.com/platform-q-ai/rusty-notes/issues/2) and the
[Clean-Architecture-Target wiki page](https://github.com/platform-q-ai/rusty-notes/wiki/Clean-Architecture-Target)
(canonical copy). It is a blocking project acceptance requirement for PR #1 and
its remediation, not optional style advice. A valid inward dependency graph
alone does not satisfy it: names and nesting must make responsibilities
discoverable to future engineers. It is a project-specific exemplary Rust
layout, not a claim of one universal CA directory standard.

The layout below is implemented by the `crates/` tree; the legacy
`domain/`, `application/`, `infrastructure/`, and `app/` directories at the
repository root were deleted in the restructure (ADR-0005). Automated
enforcement lives in `scripts/arch-check.py`, run by CI
(`.github/workflows/ci.yml`) — see "Automated enforcement".

## Target tree

```text
crates/
  domain/
    src/
      lib.rs
      notes/
        mod.rs
        note.rs
        note_id.rs
        note_draft.rs
      folders/
        mod.rs
        folder.rs
        folder_id.rs
      shared/
        mod.rs
        timestamp.rs
      error.rs
  application/
    src/
      lib.rs
      ports/
        mod.rs
        note_repository.rs
        search_service.rs
        clock.rs
        id_generator.rs
      use_cases/
        mod.rs
        notes/
          mod.rs
          create_note.rs
          open_note.rs
          update_note.rs
          move_note.rs
          delete_note.rs
          list_notes.rs
          search_notes.rs
        folders/
          mod.rs
          create_folder.rs
          rename_folder.rs
          delete_folder.rs
          list_folders.rs
      error.rs
  adapters/
    sqlite/
      src/
        lib.rs
        connection.rs
        migrations.rs
        note_repository.rs
        search_service.rs
        error_mapping.rs
      tests/
        repository_contract.rs
        search_contract.rs
        persistence.rs
    presentation/
      src/
        lib.rs
        editor_state.rs
        note_list_item.rs
        folder_list_item.rs
        notes_presenter.rs
    system/
      src/
        lib.rs
        system_clock.rs
        unique_ids.rs
  desktop/
    src/
      main.rs
      composition.rs
      app.rs
      views/
        mod.rs
        folder_sidebar.rs
        note_list.rs
        note_editor.rs
        error_banner.rs
  test-support/
    src/
      lib.rs
      fakes/
        mod.rs
        in_memory_note_repository.rs
        in_memory_search.rs
        fixed_clock.rs
        sequential_ids.rs
      contracts/
        mod.rs
        repository_contract.rs
        search_contract.rs
```

## Rules and acceptance

- domain owns business entities and invariants and has no application/adapter/UI
  dependencies.
- application depends on domain only; owns use cases and required ports, not UI
  editor state or fake adapters.
- adapters depend inward on application/domain; domain/application never depend
  outward. Presentation stays framework-free but is explicitly presentation, not
  application policy.
- desktop owns framework rendering and the composition root. Production
  clock/ID adapters must not come from test-support. System adapters may be
  grouped pragmatically with composition if a separate crate is needless;
  explain the deviation.
- test-support depends inward and supplies shared behavioral contracts/fakes
  through dev-dependencies only. Production dependency graph must exclude it.
  Avoid application/test-support dev-dependency cycles by locating cross-crate
  suites in adapter integration tests or a dedicated integration-test package if
  necessary.
- Each use case has its own descriptive snake_case module; no all-use-cases
  implementation file. Notes/folders have cohesive domain modules.
  lib.rs/mod.rs primarily declare/re-export modules, not aggregate
  implementations.
- Keep associated unit tests near their modules; cross-adapter and persistence
  tests in named integration suites.
- Split SQLite connection/migration/repository/search responsibilities and
  desktop composition/rendering/presenter state clearly. Do not add generic
  frameworks, needless one-line abstractions, or empty placeholder modules to
  match the tree.
- Update Cargo manifests, imports, README architecture map, ADRs, and CI
  dependency-boundary checks to match actual code. Checks must enforce inward
  dependencies and absence of test-support in production.
- Gates: `cargo fmt --all -- --check`;
  `cargo clippy --workspace --all-targets -- -D warnings`;
  `cargo test --workspace`; `cargo build -p rusty-notes-desktop`; automated
  architecture checks. Do not claim headless build is visual verification.

## Automated enforcement

`scripts/arch-check.py` checks the workspace dependency graph against this
target using `cargo metadata --no-deps` (no third-party tools, no build). CI
runs it on every push/pull request:

1. **Inward-only edges.** Every normal (non-dev) dependency between workspace
   crates must point strictly inward: `domain` ← `application` ← `adapters/*` ←
   `desktop`. Within the `adapters/` group (sqlite, presentation, system) the
   crates are peers: they may depend on `domain`/`application` and on each
   other only where a port demands it, never on `desktop`.
2. **No test-support in the production graph.** `rusty-notes-test-support` (and
   any crate whose name marks it as a fake/contract/mock provider) must appear
   only in `[dev-dependencies]`, never in the resolved normal dependency graph
   of any workspace member.
3. **No application ↔ test-support dev-dependency cycle.** A dev-dependency
   from `application` on `test-support` whose own dev-dependencies reach back
   into `application` cannot resolve; cross-crate contract suites therefore live
   in adapter integration tests (`crates/adapters/sqlite/tests/`,
   `crates/desktop/tests/`).

CI also runs the check negatively: a deliberately corrupted scratch copy of the
metadata (dev-dependency flipped to a production dependency, producing an
outward edge and a test-support-in-production violation) must make the script
exit non-zero. This proves the check can actually fail — see the
`architecture-check (negative)` step in `.github/workflows/ci.yml`.
