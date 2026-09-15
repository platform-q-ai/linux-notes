# Backup and restore

Linux Notes stores all user content **locally**. There is no cloud account.

## Data layout

Resolved at runtime via XDG (`src/adapters/system/xdg_storage_paths.hpp`):

| Item | Default path |
|------|----------------|
| App data root | `~/.local/share/linux-notes/` (`$XDG_DATA_HOME/linux-notes`) |
| SQLite database | `.../notes.db` |
| Attachment blobs | `.../attachments/*.bin` (id-named files, **not** content-addressed) |
| Config | `~/.config/linux-notes/` (`$XDG_CONFIG_HOME/linux-notes`) |
| Cache | `~/.cache/linux-notes/` (`$XDG_CACHE_HOME/linux-notes`) |

Organization name in Qt settings: `platform-q-ai`; application name: `Linux Notes`.
Qt may also write under the usual Qt/QSettings locations for the org/app pair.

## Backup (recommended)

1. **Quit the app** so SQLite is not mid-write.
2. Copy the entire data directory (and optionally config):

```bash
# Example archive
tar -czf linux-notes-backup-$(date -u +%Y%m%dT%H%M%SZ).tar.gz \
  -C "${XDG_DATA_HOME:-$HOME/.local/share}" linux-notes \
  -C "${XDG_CONFIG_HOME:-$HOME/.config}" linux-notes 2>/dev/null || true
```

Include `notes.db` **and** the `attachments/` directory together. Restoring the
database without matching attachment files leaves broken attachment references.

## Restore

1. Quit the app.
2. Move aside any existing data directory you want to keep.
3. Extract the archive so `notes.db` and `attachments/` land under
   `$XDG_DATA_HOME/linux-notes/` (or your custom `XDG_DATA_HOME`).
4. Start the app and spot-check folders, notes, and any attachments.

## Integrity notes

- Attachments are stored as `{attachment-id}.bin` under `attachments/`.
- IDs are generated at put-time (timestamp + sequence), not content hashes.
- There is no built-in export/import UI in this baseline; filesystem copy is the
  supported backup path.
- If a future schema migration runs on open, keep a pre-migration backup first.
