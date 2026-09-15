#!/usr/bin/env bash
# Headless UI smoke for linux-notes (task 10 prep).
# Distinguishes QT offscreen process-alive vs optional Xvfb.
# Does NOT claim real Wayland/X11 on-screen interaction without proof.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NOTES_BIN:-$ROOT/build/notes}"
EVID_DIR="${NOTES_EVIDENCE_DIR:-$ROOT/../daily-driver-evidence/linux-readiness/headless}"
mkdir -p "$EVID_DIR"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
REPORT="$EVID_DIR/ui-smoke-$STAMP.txt"

have_bin() { command -v "$1" >/dev/null 2>&1; }

{
  echo "linux-notes headless UI smoke"
  echo "utc=$STAMP"
  echo "bin=$BIN"
  echo "uname=$(uname -a)"
  echo "QT_PLUGIN_PATH=${QT_PLUGIN_PATH:-}"
  echo "QML2_IMPORT_PATH=${QML2_IMPORT_PATH:-}"
  echo "DISPLAY=${DISPLAY:-}"
  echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
  echo "--- toolchain ---"
  (cmake --version 2>/dev/null | head -1) || echo "cmake: missing"
  (ninja --version 2>/dev/null) || echo "ninja: missing"
  g++ --version 2>/dev/null | head -1 || true
  echo "xvfb-run: $(have_bin xvfb-run && echo yes || echo no)"
  echo "Xvfb: $(have_bin Xvfb && echo yes || echo no)"
  echo "---"

  if [[ ! -x "$BIN" ]]; then
    echo "RESULT: SKIP binary missing or not executable: $BIN"
    echo "No claim of UI validation."
    exit 0
  fi

  run_timeout() {
    local secs="$1"; shift
    if have_bin timeout; then
      timeout --signal=TERM --kill-after=5 "$secs" "$@"
    else
      "$@" &
      local pid=$!
      sleep "$secs" || true
      kill -TERM "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  }

  echo "=== A: QT_QPA_PLATFORM=offscreen (process/UI engine load smoke) ==="
  set +e
  OUT_A="$EVID_DIR/offscreen-$STAMP.log"
  QT_QPA_PLATFORM=offscreen \
    XDG_DATA_HOME="$EVID_DIR/xdg-data-offscreen" \
    XDG_CONFIG_HOME="$EVID_DIR/xdg-config-offscreen" \
    XDG_CACHE_HOME="$EVID_DIR/xdg-cache-offscreen" \
    run_timeout 8 "$BIN" >"$OUT_A" 2>&1
  EC_A=$?
  set -e
  echo "exit_code_or_timeout=$EC_A (124 often means timeout after healthy start)"
  echo "log=$OUT_A"
  if grep -qi 'Failed to load QML UI\|Failed to start\|QXcbConnection\|Could not connect' "$OUT_A" 2>/dev/null; then
    echo "offscreen_qml_or_start: FAIL_SIGNAL_IN_LOG"
  else
    echo "offscreen_qml_or_start: no hard failure string seen (not proof of full UI)"
  fi
  echo "NOTE: offscreen proves process/QPA offscreen path only — not interactive widgets."

  echo "=== B: Xvfb virtual display (if available) ==="
  if have_bin xvfb-run; then
    OUT_B="$EVID_DIR/xvfb-$STAMP.log"
    set +e
    XDG_DATA_HOME="$EVID_DIR/xdg-data-xvfb" \
      XDG_CONFIG_HOME="$EVID_DIR/xdg-config-xvfb" \
      XDG_CACHE_HOME="$EVID_DIR/xdg-cache-xvfb" \
      QT_QPA_PLATFORM=xcb \
      run_timeout 8 xvfb-run -a "$BIN" >"$OUT_B" 2>&1
    EC_B=$?
    set -e
    echo "xvfb_exit_or_timeout=$EC_B"
    echo "log=$OUT_B"
    echo "NOTE: Xvfb is a virtual X11 server inside the environment — not a user seat."
    echo "Do not report this as real Wayland/X11 on-screen validation."
  else
    echo "xvfb: UNAVAILABLE — skipped"
  fi

  echo "=== Claims boundary ==="
  echo "Verified-at-most: scripted start under offscreen and/or Xvfb as logged."
  echo "NOT verified: physical display, compositor, real user input, Wayland seat."
} | tee "$REPORT"

echo "Wrote $REPORT"
