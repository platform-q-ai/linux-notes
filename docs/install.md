# Install (local / packaging baseline)

Conventional CMake install for the `notes` binary, QML resources (embedded in
the binary via Qt resource system), desktop entry, and icons.

## What gets installed

| Path (under prefix) | Content |
|---------------------|---------|
| `bin/notes` | Application binary |
| `share/applications/linux-notes.desktop` | Desktop entry |
| `share/icons/hicolor/*/apps/linux-notes.png` | App icons |
| `share/doc/linux-notes/` | README, install/backup/notices docs (optional copy) |

QML UI is compiled into the binary (`presentation_qml.qrc`). An installed
binary does **not** need `NOTES_QML_DIR` or a source tree. `NOTES_QML_DIR` is
only a **development fallback** when the qrc load fails during source builds.

## Configure, build, local DESTDIR artifact

Do **not** install onto the host unless you intend to. Prefer a DESTDIR stage
under the workspace evidence tree:

```bash
# Toolchain: see docs/dev-setup.md (Qt 6.7+, CMake 3.21+, Ninja, g++-14)
source /path/to/env.sh   # optional container helper

cmake -G Ninja -S . -B build-rel \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DNOTES_BUILD_TESTS=OFF \
  -DNOTES_BUILD_UI=ON

cmake --build build-rel

# Stage only (no host write outside the destination directory):
DESTDIR="$PWD/../daily-driver-evidence/linux-readiness/destdir" \
  cmake --install build-rel

# Binary under staged tree:
../daily-driver-evidence/linux-readiness/destdir/usr/local/bin/notes
```

Prefix defaults to `/usr/local`. Change with `-DCMAKE_INSTALL_PREFIX=...`.

## Runtime data (not part of install prefix)

User data lives under XDG paths (see [backup-restore.md](backup-restore.md)):

- `$XDG_DATA_HOME/linux-notes/` (default `~/.local/share/linux-notes/`)
- `$XDG_CONFIG_HOME/linux-notes/`
- `$XDG_CACHE_HOME/linux-notes/`

## Uninstall

See [uninstall.md](uninstall.md). Removing the prefix files does **not** delete
user notes/attachments unless you remove the XDG data directories deliberately.
