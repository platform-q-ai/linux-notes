#!/usr/bin/env bash
# Intentional architecture violation fixtures.
# Each case must FAIL the canonical checker; clean tree must PASS.
# No blanket ignore lists — every rule has a positive and negative proof.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CHECKER="$SCRIPT_DIR/architecture_check.sh"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/arch-viol.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

pass=0
fail=0

expect_pass() {
  local label="$1"
  if bash "$CHECKER" "$REPO_ROOT" >"$WORKDIR/out.txt" 2>&1; then
    echo "PASS expect-clean: $label"
    pass=$((pass + 1))
  else
    echo "FAIL expect-clean: $label (checker exited non-zero on real tree)"
    cat "$WORKDIR/out.txt"
    fail=$((fail + 1))
  fi
}

# Build a minimal fake tree that still has the layer directories the checker walks.
seed_tree() {
  local dest="$1"
  mkdir -p "$dest/src/domain" "$dest/src/application" \
           "$dest/src/adapters" "$dest/src/presentation"
  # Empty placeholders so rg paths exist even when a case only pollutes one layer.
  : >"$dest/src/domain/.keep"
  : >"$dest/src/application/.keep"
  : >"$dest/src/adapters/.keep"
  : >"$dest/src/presentation/.keep"
}

expect_violation() {
  local label="$1"
  local setup_fn="$2"
  local tree="$WORKDIR/$label"
  rm -rf "$tree"
  seed_tree "$tree"
  "$setup_fn" "$tree"
  if bash "$CHECKER" "$tree" >"$WORKDIR/out.txt" 2>&1; then
    echo "FAIL expect-violation: $label (checker PASSED; rule not enforced)"
    cat "$WORKDIR/out.txt"
    fail=$((fail + 1))
  else
    if grep -q 'architecture-check FAILED' "$WORKDIR/out.txt"; then
      echo "PASS expect-violation: $label"
      pass=$((pass + 1))
    else
      echo "FAIL expect-violation: $label (non-zero but unexpected output)"
      cat "$WORKDIR/out.txt"
      fail=$((fail + 1))
    fi
  fi
}

v_domain_qt() {
  printf '%s\n' '#include <QString>' 'struct X {};' >"$1/src/domain/bad.hpp"
}
v_application_qt() {
  printf '%s\n' '#include <QObject>' 'struct Y {};' >"$1/src/application/bad.hpp"
}
v_adapter_use_case() {
  printf '%s\n' '#include "application/use_cases/save_note.hpp"' >"$1/src/adapters/bad.cpp"
}
v_presentation_sqlite() {
  printf '%s\n' '#include "adapters/persistence/sqlite/sqlite_note_store.hpp"' \
    >"$1/src/presentation/bad.cpp"
}
v_domain_sqlite() {
  printf '%s\n' '#include <sqlite3.h>' >"$1/src/domain/bad_sql.hpp"
}
v_application_sqlite() {
  printf '%s\n' '#include <sqlite3.h>' >"$1/src/application/bad_sql.hpp"
}
v_domain_fstream() {
  printf '%s\n' '#include <fstream>' >"$1/src/domain/bad_io.hpp"
}
v_prod_fakes() {
  printf '%s\n' '#include "tests/support/fakes/in_memory_note_store.hpp"' \
    'using InMemoryNoteStore = int;' >"$1/src/adapters/bad_fake.cpp"
}

echo "== architecture violation suite =="
expect_pass "repository-clean"

expect_violation "domain-qt-include" v_domain_qt
expect_violation "application-qt-include" v_application_qt
expect_violation "adapter-use-case-include" v_adapter_use_case
expect_violation "presentation-sqlite-include" v_presentation_sqlite
expect_violation "domain-sqlite-include" v_domain_sqlite
expect_violation "application-sqlite-include" v_application_sqlite
expect_violation "domain-fstream-include" v_domain_fstream
expect_violation "production-test-fake-ref" v_prod_fakes

echo "== summary: pass=$pass fail=$fail =="
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
# sanity: we must have exercised clean + 8 violations
if [[ "$pass" -lt 9 ]]; then
  echo "FAIL: expected at least 9 assertions, got pass=$pass"
  exit 1
fi
echo "architecture-violation-tests PASSED"
exit 0
