#!/usr/bin/env bash
# Regression: the documented backup tar recipe in docs/backup-restore.md must
# fail closed. This script extracts and runs that recipe (not a parallel invent).
#
# Contracts:
#   - data present + config absent → exit 0 and archive contains notes.db
#     (must NOT silently exit 0 with empty/unusable archive)
#   - data absent → non-zero exit (error must propagate; no || true swallow)
#   - data + config present → exit 0 and archive contains notes.db
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOC="$ROOT/docs/backup-restore.md"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/backup-recipe.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

pass=0
fail=0

assert_ok() {
  local label="$1"
  printf 'PASS: %s\n' "$label"
  pass=$((pass + 1))
}

assert_fail() {
  local label="$1"
  local detail="${2:-}"
  printf 'FAIL: %s%s\n' "$label" "${detail:+ — ${detail}}"
  fail=$((fail + 1))
}

# Extract the fenced recipe that archives linux-notes via tar -czf under XDG paths.
extract_recipe() {
  local out="$1"
  awk '
    /^```/ {
      if (in_block) {
        if (buf ~ /tar[ \t]+-czf/ && buf ~ /XDG_DATA_HOME/ && buf ~ /linux-notes/) {
          printf "%s", buf
          found=1
          exit
        }
        buf=""
        in_block=0
        next
      }
      in_block=1
      next
    }
    in_block { buf = buf $0 "\n" }
    END { if (!found) exit 1 }
  ' "$DOC" >"$out"
}

RECIPE_FILE="$WORKDIR/recipe.body"
if [[ ! -f "$DOC" ]]; then
  echo "FAIL: missing doc $DOC" >&2
  exit 1
fi
if ! extract_recipe "$RECIPE_FILE"; then
  echo "FAIL: could not extract tar -czf recipe from $DOC" >&2
  exit 1
fi

echo "== extracted documented recipe =="
cat "$RECIPE_FILE"
echo "== end recipe =="

# Optional integrity probe suggested by the finding (used when archive exists).
archive_has_notes_db() {
  local archive="$1"
  [[ -f "$archive" ]] || return 1
  local sz
  sz="$(wc -c <"$archive" | tr -d ' ')"
  [[ "$sz" -gt 22 ]] || return 1  # empty gzip is ~20 bytes
  tar -tzf "$archive" >/dev/null 2>&1 || return 1
  tar -tzf "$archive" | grep -q 'notes\.db'
}

run_case() {
  local label="$1"
  local data_setup="$2"    # present|absent
  local config_setup="$3"  # present|absent
  local expect="$4"        # success|failure

  local case_dir="$WORKDIR/$label"
  rm -rf "$case_dir"
  mkdir -p "$case_dir/run"
  local data_home="$case_dir/xdg-data"
  local config_home="$case_dir/xdg-config"
  # XDG_*_HOME parents: create data home only when we need a place for the
  # data tree. Config home is created only when config is present — a missing
  # config directory (common before first settings write) is the documented
  # failure mode where GNU tar -C fails after creating an empty/unusable archive.
  mkdir -p "$data_home"

  if [[ "$data_setup" == "present" ]]; then
    mkdir -p "$data_home/linux-notes/attachments"
    printf 'notes-db-bytes\n' >"$data_home/linux-notes/notes.db"
    printf 'blob\n' >"$data_home/linux-notes/attachments/att1.bin"
  fi
  if [[ "$config_setup" == "present" ]]; then
    mkdir -p "$config_home/linux-notes"
    printf 'cfg\n' >"$config_home/linux-notes/settings.ini"
  fi
  # When config is absent, leave $config_home missing entirely (do not mkdir).

  local runner="$case_dir/run_recipe.sh"
  {
    echo '#!/usr/bin/env bash'
    # Intentionally do NOT add set -e here: the documented recipe is what under
    # test, including any shell operators it uses (e.g. || true).
    echo "export XDG_DATA_HOME='$data_home'"
    echo "export XDG_CONFIG_HOME='$config_home'"
    echo "cd '$case_dir/run'"
    cat "$RECIPE_FILE"
  } >"$runner"

  set +e
  bash "$runner" >"$case_dir/stdout.txt" 2>"$case_dir/stderr.txt"
  local ec=$?
  set -e

  local archive=""
  archive="$(find "$case_dir/run" -maxdepth 1 -type f -name 'linux-notes-backup-*.tar.gz' | head -n1 || true)"

  local usable=0
  if [[ -n "$archive" ]] && archive_has_notes_db "$archive"; then
    usable=1
  fi

  printf -- '-- case %s: exit=%s archive=%s usable=%s --\n' \
    "$label" "$ec" "${archive:-none}" "$usable"
  if [[ -n "$archive" ]]; then
    printf '   size=%s listing:\n' "$(wc -c <"$archive" | tr -d ' ')"
    tar -tzf "$archive" 2>&1 | sed 's/^/   /' || true
  fi
  if [[ -s "$case_dir/stderr.txt" ]]; then
    echo '   stderr:'; sed 's/^/   /' "$case_dir/stderr.txt" || true
  fi

  if [[ "$expect" == "success" ]]; then
    if [[ "$ec" -eq 0 && "$usable" -eq 1 ]]; then
      assert_ok "$label (usable archive with notes.db, exit 0)"
    else
      assert_fail "$label (expected usable success)" "exit=$ec usable=$usable"
    fi
    return 0
  fi

  # expect=failure: non-zero exit required; exit 0 with empty/unusable is RED
  if [[ "$ec" -eq 0 ]]; then
    if [[ "$usable" -eq 1 ]]; then
      assert_fail "$label (expected failure, got usable success)"
    else
      assert_fail "$label (exit 0 with empty/unusable archive — silent backup failure)" \
        "archive=${archive:-none}"
    fi
  else
    assert_ok "$label (fail-closed exit=$ec)"
  fi
}

echo "== backup-restore documented recipe regression =="
# Finding: missing config must not yield exit 0 empty archive; after fix,
# data-dir alone must produce a usable backup (config optional).
run_case "data-present-config-absent" present absent success
# Data absent must propagate tar/shell failure (no || true swallow).
run_case "data-absent-config-absent" absent absent failure
# Happy path with both trees.
run_case "data-present-config-present" present present success

echo "== summary: pass=$pass fail=$fail =="
if [[ "$fail" -ne 0 ]]; then
  echo "backup-restore-recipe-regression FAILED"
  exit 1
fi
if [[ "$pass" -lt 3 ]]; then
  echo "FAIL: expected 3 assertions, got pass=$pass"
  exit 1
fi
echo "backup-restore-recipe-regression PASSED"
exit 0
