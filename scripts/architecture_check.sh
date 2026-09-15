#!/usr/bin/env bash
# Thin entrypoint used by CI and docs — delegates to the canonical checker.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec bash "$ROOT/scripts/architecture/architecture_check.sh" "$ROOT"
