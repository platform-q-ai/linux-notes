# Linux Notes

Local-only notes app for Linux (C++20 / Qt6 / QML), inspired by the Apple Notes
three-pane workflow. **No accounts or cloud.**

## Features (v0.1 baseline)

- Three-pane UI: folders, notes list, editor
- Folder and note CRUD (create/rename/delete folders; create/delete notes)
- Plain/rich-ish text editing with undo/redo hooks and basic B/I/U markers
- Search across title/content
- Pin flag on notes (sort/list aware)
- Debounced autosave (~2s, max ~10s) with visible saving/error state
- Close-gate flush so pending saves are attempted before quit
- SQLite persistence under XDG data dir
- Local attachment **blobs on disk** (`attachments/{id}.bin`) plus domain/
  persistence support for checklist items and attachment refs
  (**not** content-addressed / hash-keyed storage)
- Checklist toggle and attach-file use cases exist in application layer;
  editor UI polish for checklist/attach may still be partial depending on
  milestone progress

## Non-goals (this release)

Scanning, handwriting, cloud sync, AI, full Apple Notes parity, tables beyond
content preservation, external package-repo publication.

## Build

See [docs/dev-setup.md](docs/dev-setup.md).

```bash
cmake -G Ninja -S . -B build -DNOTES_BUILD_TESTS=ON
cmake --build build
./build/notes
```

Headless smoke: `QT_QPA_PLATFORM=offscreen ./build/notes` (process/UI load only;
not a substitute for a real display session).

## Install (local artifact)

See [docs/install.md](docs/install.md). Conventional CMake install stages binary,
desktop entry, and icons. QML is embedded via Qt resources—no source-tree
`NOTES_QML_DIR` required for installed runs.

```bash
cmake -G Ninja -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build-rel
DESTDIR=./stage cmake --install build-rel
```

## Data, backup, uninstall

- Runtime data: `$XDG_DATA_HOME/linux-notes` (default `~/.local/share/linux-notes`)
- [docs/backup-restore.md](docs/backup-restore.md)
- [docs/uninstall.md](docs/uninstall.md)

## Third-party / license

- [docs/third-party-notices.md](docs/third-party-notices.md) — inventory only; **not** legal certification
- **No project `LICENSE` file is present** in-tree at inspection time; adding one
  requires an explicit user decision (do not invent copyright terms)

## Layout

Matches the project architecture: `src/domain`, `src/application`,
`src/adapters`, `src/presentation`, `src/desktop`, tests under `tests/`.
Packaging assets under `packaging/`.
