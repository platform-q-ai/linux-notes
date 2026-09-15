# Backup and restore

Linux Notes stores all user content **locally**. There is no cloud account.

## Data layout

Resolved at runtime via XDG (`src/adapters/system/xdg_storage_paths.hpp`):

| Item | Default path |
|------|----------------|
| App data root | `~/.local/share/linux-notes/` (`$XDG_DATA_HOME/linux-notes`) |
| SQLite database | `.../notes.db` |
| SQLite WAL / SHM (when present) | `.../notes.db-wal`, `.../notes.db-shm` |
| Attachment blobs | `.../attachments/*.bin` (id-named files, **not** content-addressed) |
| Config | `~/.config/linux-notes/` (`$XDG_CONFIG_HOME/linux-notes`) |
| Cache | `~/.cache/linux-notes/` (`$XDG_CACHE_HOME/linux-notes`) |

Organization name in Qt settings: `platform-q-ai`; application name: `Linux Notes`.
Qt may also write under the usual Qt/QSettings locations for the org/app pair.

The app opens SQLite with **`PRAGMA journal_mode=WAL`** (`sqlite_db.cpp`). While the
process is running (or after a crash without a clean checkpoint), committed pages
may live in `notes.db-wal` with `notes.db-shm` as the shared-memory index. Copying
only `notes.db` can yield a **torn or stale** backup.

## Backup (recommended)

Prefer a **consistent** snapshot. Pick one of the following.

### A. Graceful shutdown + full directory archive (simplest)

1. **Quit the app completely** so connections close and SQLite can checkpoint.
2. Confirm no `notes` process still holds the DB.
3. Archive the entire data directory (and optionally config). Include
   `notes.db`, any `notes.db-wal` / `notes.db-shm` still present, **and**
   `attachments/`:

```bash
# Example archive — whole data dir catches db + WAL/SHM + attachment blobs.
# Require the data directory; add config only when it exists. Never swallow tar
# failures (no `|| true`) — an empty archive must not look like success.
set -euo pipefail
data_root="${XDG_DATA_HOME:-$HOME/.local/share}"
config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
archive="linux-notes-backup-$(date -u +%Y%m%dT%H%M%SZ).tar.gz"
# Fail closed if the data tree is missing or unreadable.
test -d "${data_root}/linux-notes"
tar_args=(-C "${data_root}" linux-notes)
# Optional config: include only when present (common before first settings write).
if [ -d "${config_root}/linux-notes" ]; then
  tar_args+=(-C "${config_root}" linux-notes)
fi
tar -czf "${archive}" "${tar_args[@]}"
# Integrity: archive must list notes.db (catches empty/truncated outputs).
tar -tzf "${archive}" | grep -q 'notes\.db'
```

After a clean quit, `-wal`/`-shm` are often absent or empty; still archive the
whole directory so you never miss them.

### B. Online consistent copy (app may stay closed or you accept a hot backup tool)

Do **not** use a plain `cp notes.db` (or copy the three files without interlocking)
while the app might write. For a hot/consistent file-level backup use one of:

1. **SQLite Backup API** (preferred programmatic path), e.g. the shell:

```bash
# Produces a single consistent notes-backup.db (WAL contents folded in)
sqlite3 "${XDG_DATA_HOME:-$HOME/.local/share}/linux-notes/notes.db" \
  ".backup '${PWD}/notes-backup.db'"
# Then copy attachments/ the same moment you take the backup
```

2. **`VACUUM INTO`** (SQLite 3.27+), writing a new compact DB file.
3. A filesystem freeze/snapshot of the whole data dir (LVM/ZFS/btrfs), not a
   racy live `cp`.

Always pair the DB backup with a copy of `attachments/` taken with the same
backup set. Restoring the database without matching attachment blobs leaves
broken attachment references.

### C. If you must copy files after quit

After quit, copy **all** of:

- `notes.db`
- `notes.db-wal` (if it exists)
- `notes.db-shm` (if it exists)
- `attachments/` (entire directory)

Or checkpoint first, then copy the main DB only:

```bash
sqlite3 "${XDG_DATA_HOME:-$HOME/.local/share}/linux-notes/notes.db" \
  'PRAGMA wal_checkpoint(TRUNCATE);'
# Now notes.db holds the committed state; still back up attachments/
```

## Restore

1. Quit the app.
2. Move aside any existing data directory you want to keep.
3. Extract the archive (or place `notes.db` plus any restored `-wal`/`-shm` and
   `attachments/`) under `$XDG_DATA_HOME/linux-notes/` (or your custom
   `XDG_DATA_HOME`). If you used `.backup` / `VACUUM INTO`, install that file as
   `notes.db` and **do not** leave a stale `notes.db-wal` from a different epoch
   beside it.
4. Start the app and spot-check folders, notes, and any attachments.

## Integrity notes

- Attachments are stored as `{attachment-id}.bin` under `attachments/`.
- IDs are generated at put-time (timestamp + sequence), not content hashes.
- There is no built-in export/import UI in this baseline; filesystem snapshot or
  the SQLite backup API is the supported backup path.
- **Unsafe:** live `cp` of `notes.db` alone (or of db/wal/shm without a
  consistent snapshot/backup API) while the app is running.
- If a future schema migration runs on open, keep a pre-migration backup first.
