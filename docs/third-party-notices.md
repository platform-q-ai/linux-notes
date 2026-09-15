# Third-party components and notices

This document is an **informational inventory** of third-party software the
project builds against or vendors. It is **not** legal advice, **not** a
license grant, and **not** a certification of compliance, provenance, or
redistribution rights. Operators who ship binaries must perform their own
license review against the exact versions and packaging they distribute.

## Project license status

As of the linux-readiness baseline inspection:

- **No top-level `LICENSE` / `COPYING` file** is present in this repository.
- No project copyright header policy is asserted here.
- **Do not invent or add a license file without an explicit user decision.**

## Runtime / build dependencies (system or SDK)

| Component | Role | Where |
|-----------|------|--------|
| **Qt 6** (Core, Gui, Qml, Quick, QuickControls2) | UI toolkit | External SDK / distro packages (e.g. Qt 6.7.x) |
| **SQLite3** | Persistence | System `libsqlite3` / `SQLite::SQLite3` |
| **C++ standard library / libstdc++** | Runtime | Toolchain |
| **Platform plugins** (xcb, wayland, offscreen, …) | Qt QPA | Shipped with Qt install |

Qt is available under LGPL/commercial and other terms depending on how you
obtain it. See the Qt Company licensing pages and the licenses bundled with
**your** Qt installation (often under `Licenses/` or distro doc packages).
This project does not embed full Qt sources.

SQLite’s authors dedicate the software to the public domain in the classic
SQLite sense; confirm against the libsqlite3 package you link.

## Vendored in-tree

| Component | Role | Path |
|-----------|------|------|
| **Catch2** (amalgamated) | Test framework only | `third_party/catch2/` |

Catch2 is used for tests. It is not linked into the production `notes`
binary when tests are disabled. Consult Catch2’s upstream license (BSL-1.0
for modern Catch2) in upstream docs; the amalgamation here may not include a
separate license file copy—verify before redistribution of test tooling.

## Application data vs third-party code

User notes and attachment bytes are **user data**, not third-party code.
Attachment storage is local filesystem blobs keyed by attachment id (see
[backup-restore.md](backup-restore.md)).

## Honest limitations

- No automated SBOM or license scanner is run in the default CI of this repo.
- No claim is made that a DESTDIR artifact is redistributable as-is.
- Icons under `packaging/icons/` are simple project artwork for the desktop
  entry; treat them as part of the application assets pending a project license
  decision.
