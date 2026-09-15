#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0

check_no_match() {
  local dir="$1"
  local pattern="$2"
  local msg="$3"
  if rg -n --glob '*.hpp' --glob '*.h' --glob '*.cpp' -e "$pattern" "$ROOT/$dir" >/tmp/arch_hits.txt 2>/dev/null; then
    echo "ARCHITECTURE FAIL: $msg"
    cat /tmp/arch_hits.txt
    fail=1
  fi
}

check_no_match "src/domain" '#include\s*<Q|Qt[A-Z]|sqlite3|fstream|filesystem' "domain must not include Qt/IO/sqlite"
check_no_match "src/application" '#include\s*<Q|Qt[A-Z]|sqlite3' "application must not include Qt/sqlite"
check_no_match "src/adapters" 'use_cases/' "adapters must not include use_case headers"
check_no_match "src/presentation" 'sqlite_note_store|adapters/persistence' "presentation must not include sqlite adapters"

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "architecture-check OK"
