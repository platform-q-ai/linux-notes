# Linux Notes

Local-only notes app for Linux (C++20 / Qt6 / QML), inspired by the Apple Notes three-pane workflow. **No accounts or cloud.**

## Features (v0.1)

- Three-pane UI: folders, notes list, editor
- Folder and note CRUD
- Plain/rich-ish text editing with undo/redo hooks and basic markers
- Search across title/content
- Debounced autosave (~2s, max ~10s) with visible saving/error state
- SQLite persistence + content-addressed local attachments store
- Checklist items and attachments in domain/persistence (UI checklist/attach polish is partial)

## Build

See [docs/dev-setup.md](docs/dev-setup.md).

```bash
cmake -G Ninja -S . -B build -DNOTES_BUILD_TESTS=ON
cmake --build build
./build/notes
```

## Layout

Matches the project wiki architecture: `src/domain`, `src/application`, `src/adapters`, `src/presentation`, `src/desktop`, mirrored tests under `tests/`.

## Non-goals (this release)

Scanning, handwriting, cloud sync, AI, full Apple Notes parity.
