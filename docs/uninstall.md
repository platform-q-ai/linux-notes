# Uninstall and data retention

## Remove installed application files

If you installed with a prefix (example `/usr/local`) or a DESTDIR stage:

```bash
# Files typically installed by this project:
#   $PREFIX/bin/notes
#   $PREFIX/share/applications/linux-notes.desktop
#   $PREFIX/share/icons/hicolor/*/apps/linux-notes.png
#   $PREFIX/share/doc/linux-notes/*   (when docs install is enabled)

sudo rm -f /usr/local/bin/notes
sudo rm -f /usr/local/share/applications/linux-notes.desktop
sudo rm -f /usr/local/share/icons/hicolor/*/apps/linux-notes.png
sudo rm -rf /usr/local/share/doc/linux-notes
```

Prefer reversing the same DESTDIR/prefix you used for install. Package managers
(if you later package this yourself) should own file removal.

Removing the binary and desktop entry does **not** remove your notes.

## User data retention (default)

**Kept after uninstall** unless you delete them:

| Path | Contents |
|------|----------|
| `$XDG_DATA_HOME/linux-notes/` | `notes.db`, `attachments/` |
| `$XDG_CONFIG_HOME/linux-notes/` | app config if any |
| `$XDG_CACHE_HOME/linux-notes/` | cache |
| Qt settings for org `platform-q-ai` / app `Linux Notes` | toolkit settings |

## Delete user data (irreversible)

Only after a backup if you may need the notes again:

```bash
rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/linux-notes"
rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/linux-notes"
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/linux-notes"
# Optional Qt settings cleanup (paths vary by platform/Qt):
#   ~/.config/platform-q-ai/  or similar QSettings location
```

There is no remote wipe; local deletion is permanent.
