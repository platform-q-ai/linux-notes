-- Canonical on-disk shape for Linux Notes (schema_migrations version = 2).
-- Applied via versioned migrate() in sqlite_db.cpp:
--   v1: folders + notes + indexes + root seed
--   v2: notes_search (+ FK) and backfill; repairs prior-PR DBs stamped v1
--       without a usable search index. Never silently deletes user notes.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_migrations (
  version INTEGER PRIMARY KEY,
  applied_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS folders (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  parent_id TEXT,
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT 0,
  modified_at INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY (parent_id) REFERENCES folders(id)
);

CREATE TABLE IF NOT EXISTS notes (
  id TEXT PRIMARY KEY,
  folder_id TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  body BLOB NOT NULL,
  created_at INTEGER NOT NULL,
  modified_at INTEGER NOT NULL,
  revision INTEGER NOT NULL,
  pinned INTEGER NOT NULL DEFAULT 0,
  FOREIGN KEY (folder_id) REFERENCES folders(id)
);

CREATE INDEX IF NOT EXISTS idx_notes_folder ON notes(folder_id);
CREATE INDEX IF NOT EXISTS idx_notes_modified ON notes(modified_at DESC);

CREATE TABLE IF NOT EXISTS notes_search (
  note_id TEXT PRIMARY KEY,
  search_text TEXT NOT NULL,
  FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE
);
