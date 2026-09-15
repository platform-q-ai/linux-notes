#!/usr/bin/env bash
# architecture-check — canonical layer rules (CI job name: architecture-check)
# Usage: architecture_check.sh [ROOT]
# ROOT defaults to repository root (two levels above this script).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ "${1:-}" != "" ]]; then
  ROOT="$(cd "$1" && pwd)"
else
  ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi
cd "$ROOT"

fail=0
ok() { printf 'OK: %s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*"; fail=1; }

if ! command -v rg >/dev/null 2>&1; then
  echo "rg (ripgrep) required" >&2
  exit 2
fi

# 1) No Qt in domain / application
if rg -n --glob '*.{h,hpp,c,cpp,cc,cxx}' \
  '#\s*include\s*[<"](Qt|QObject|QString|QQml|QGui|QCore|QAbstract|QTimer|QThread)' \
  src/domain src/application 2>/dev/null; then
  bad "Qt headers found under src/domain or src/application"
else
  ok "no Qt includes in domain/application"
fi

# 2) Adapters must not include use_case headers
if rg -n --glob '*.{h,hpp,c,cpp,cc,cxx}' \
  'use_cases/|#\s*include\s*".*use_cases' \
  src/adapters 2>/dev/null; then
  bad "adapters include use_case headers"
else
  ok "adapters do not include use_case headers"
fi

# 3) Presentation must not include sqlite concrete adapters
if rg -n --glob '*.{h,hpp,c,cpp,cc,cxx,qml}' \
  'sqlite_note_store|sqlite_folder_store|sqlite_note_search|adapters/persistence/sqlite' \
  src/presentation 2>/dev/null; then
  bad "presentation references sqlite adapters"
else
  ok "presentation does not include sqlite adapters"
fi

# 4) Domain free of sqlite
if rg -n --glob '*.{h,hpp,c,cpp}' 'sqlite3\.h|<sqlite' src/domain 2>/dev/null; then
  bad "domain includes sqlite"
else
  ok "domain has no sqlite"
fi

# 5) Domain free of iostream / filesystem IO seams
if rg -n --glob '*.{h,hpp,c,cpp}' \
  '#\s*include\s*[<"](fstream|filesystem|sqlite3)' \
  src/domain 2>/dev/null; then
  bad "domain includes fstream/filesystem/sqlite"
else
  ok "domain has no fstream/filesystem/sqlite includes"
fi

# 6) Production src must not reference test fakes
if rg -n --glob '*.{h,hpp,c,cpp}' 'InMemoryNoteStore|tests/support' src 2>/dev/null; then
  bad "production src references test support/fakes"
else
  ok "no test fakes in production src"
fi

if [[ "$fail" -ne 0 ]]; then
  echo "architecture-check FAILED"
  exit 1
fi
echo "architecture-check PASSED"
exit 0
