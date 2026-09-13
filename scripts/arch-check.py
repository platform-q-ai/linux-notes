#!/usr/bin/env python3
"""Automated architecture checks for rusty-notes (fix-round finding F9).

Verifies the workspace dependency graph against the Clean Architecture target
(`docs/architecture-target.md`, issue #2) using only `cargo metadata` — no
third-party crates, no build required:

  1. **Inward-only edges.** Every workspace→workspace normal (non-dev)
     dependency must follow the layering domain ← application ← adapters ←
     desktop. Dev-dependencies may reach any layer (tests exercise the whole
     stack) and may point at `test-support`. `test-support` itself is exempt as
     a *source*: it legitimately wraps `domain` + `application` for tests.
  2. **No test-support in the production graph.** `rusty-notes-test-support`
     (or any crate whose name marks it as a fake/contract/mock provider) must
     never appear as a normal dependency of any workspace member; it may appear
     only in `[dev-dependencies]`.
  3. **No application ↔ test-support dev-dependency cycle.** A workspace crate
     that dev-depends on `test-support`, which in turn reaches back into that
     crate through the workspace graph, cannot be resolved by cargo.
     Cross-crate contract suites therefore live in adapter integration tests.

Exit code 0 = conforming; 1 = violation(s) found (each printed as
`arch-check: ERROR: …`). Run automatically in CI (`.github/workflows/ci.yml`,
which also runs a negative check on a deliberately corrupted metadata copy)
and locally via `python3 scripts/arch-check.py`.

Usage: `python3 scripts/arch-check.py [--metadata <path/to/metadata.json>]`
`--metadata` reads a `cargo metadata --no-deps --format-version 1` JSON file
instead of invoking cargo (used by the tests and the CI negative check for
this script).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# Layers, innermost first. A normal dependency from a layer L is allowed only
# on layers at or below L in this list (i.e. equal or closer to the domain core).
LAYER_ORDER: Tuple[str, ...] = ("domain", "application", "adapters", "desktop")
LAYER_RANK: Dict[str, int] = {n: i for i, n in enumerate(LAYER_ORDER)}

# Adapter-group crates whose flat package names do not embed a layer token
# (`rusty-notes-sqlite`, `rusty-notes-presentation`, `rusty-notes-system` all
# live under `crates/adapters/`).
ADAPTER_CRATES: Tuple[str, ...] = ("sqlite", "presentation", "system")

# Brand prefix used by every workspace crate: `rusty-notes-<what>`.
BRAND_PREFIXES: Tuple[str, ...] = ("rusty-notes-", "rusty_notes_")

# Crates that exist only for tests: never legal outside dev-dependencies, and
# never legal inside any production (normal/bench) resolve graph.
TEST_ONLY_PATTERNS = ("test-support", "test_support", "test-support-")

# Name fragments identifying fake/contract module crates — same policy as
# test-support: test double providers stay out of the production graph.
FAKE_CRATE_PATTERNS = ("fake", "contract", "mock")

ALL_TEST_ONLY = tuple(TEST_ONLY_PATTERNS) + tuple(FAKE_CRATE_PATTERNS)


def is_test_support(name: str) -> bool:
    return any(p in name for p in ALL_TEST_ONLY)


def run_cargo_metadata(metadata_path: Optional[str] = None) -> dict:
    """Load `cargo metadata --no-deps --format-version 1` from file or cargo."""
    if metadata_path:
        return json.loads(Path(metadata_path).read_text(encoding="utf-8"))
    proc = subprocess.run(
        ["cargo", "metadata", "--no-deps", "--format-version", "1"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print("arch-check: ERROR: `cargo metadata` failed:", file=sys.stderr)
        sys.stderr.write(proc.stderr)
        raise SystemExit(1)
    return json.loads(proc.stdout)


def layer_of(pkg_name: str) -> Optional[int]:
    """Map a workspace package name to its architectural layer rank.

    `rusty-notes-domain` -> domain, `rusty-notes-application` -> application,
    `rusty-notes-sqlite` / `rusty-notes-presentation` / `rusty-notes-system`
    -> adapters, `rusty-notes-desktop` -> desktop. Test-support-style names
    return None (they are handled by the dedicated test-only rules).
    """
    if is_test_support(pkg_name):
        return None
    tokens = [t for t in pkg_name.replace("_", "-").split("-") if t]
    for prefix in BRAND_PREFIXES:
        if pkg_name.startswith(prefix):
            tail = pkg_name[len(prefix):]
            tokens = [t for t in tail.replace("_", "-").split("-") if t]
            break
    for token in tokens:
        if token in LAYER_RANK:
            return LAYER_RANK[token]
        if token in ADAPTER_CRATES:
            return LAYER_RANK["adapters"]
    return None


def manifest_dir(manifest_path: str) -> str:
    """Directory containing a package's Cargo.toml (resolves path deps)."""
    return str(Path(manifest_path).parent)


def find_violations(md: dict) -> List[str]:
    """Return every architecture violation found in the metadata, one per line."""
    packages: Dict[str, dict] = {p["id"]: p for p in md.get("packages", [])}
    # Directory containing each package's Cargo.toml -> package id, so `path`
    # dependencies can be resolved to ids.
    by_dir: Dict[str, str] = {
        manifest_dir(p["manifest_path"]): pid
        for pid, p in packages.items()
        if p.get("manifest_path")
    }
    workspace_root = md.get("workspace_root", ".")

    def resolve_dep_pid(dep_path: str) -> Optional[str]:
        """Map a dependency's `path` to a workspace package id, or None.

        Cargo reports dependency `path` values as the package directory (the
        directory containing Cargo.toml); some generators/cargo versions emit
        the Cargo.toml file itself or a workspace-relative path, so try all
        three shapes.
        """
        candidates = [
            dep_path,
            manifest_dir(dep_path),
            str(Path(workspace_root) / dep_path),
        ]
        for cand in candidates:
            hit = by_dir.get(cand)
            if hit is not None:
                return hit
        return None

    workspace_members: List[str] = list(md.get("workspace_members", []))

    errors: List[str] = []

    def err(msg: str) -> None:
        errors.append(f"arch-check: ERROR: {msg}")

    # All workspace edges (normal + dev), used by the dev-cycle check.
    all_edges: Dict[str, List[str]] = {pid: [] for pid in packages}

    for pid in workspace_members:
        pkg = packages.get(pid)
        if pkg is None:
            err(f"workspace member {pid!r} has no package metadata")
            continue
        name = pkg["name"]
        src_layer = layer_of(name)

        for dep in pkg.get("dependencies", []):
            dep_name = dep.get("name", "")
            is_dev = dep.get("kind") == "dev"
            dep_path = dep.get("path")
            target_pid = resolve_dep_pid(dep_path) if dep_path else None

            # External (non-workspace) dependency: out of scope for the layer
            # rules — except a test-only name, which must never be a normal
            # dependency even when pulled from a registry under that name.
            if target_pid is None:
                if is_test_support(dep_name) and not is_dev:
                    err(
                        f"{name}: production dependency on test-only crate "
                        f"'{dep_name}' is forbidden (test-support must be dev-only)"
                    )
                continue

            target = packages[target_pid]
            tname = target["name"]
            all_edges[pid].append(target_pid)

            # The test-support crate is exempt as a *source*: its purpose is to
            # wrap domain + application for tests. It is still recorded above
            # for the dev-cycle check.
            if is_test_support(name):
                continue

            # --- Check 2: test-only crates anywhere outside dev-dependencies.
            if is_test_support(tname):
                if not is_dev:
                    err(
                        f"{name}: production dependency on test-only crate "
                        f"'{tname}' is forbidden (test-support must be dev-only)"
                    )
                continue

            # --- Check 1: normal workspace dependencies must point inward.
            # Dev-dependencies may reach any layer (integration tests exercise
            # the whole stack); only the production graph is constrained here.
            if not is_dev and src_layer is not None:
                tl = layer_of(tname)
                if tl is not None and tl > src_layer:
                    err(
                        f"{name}: outward dependency '{name}' -> '{tname}' "
                        f"violates inward-only layering "
                        f"({LAYER_ORDER[src_layer]} -> {LAYER_ORDER[tl]})"
                    )
                elif tl is None:
                    # Fail closed so renames cannot sneak a boundary past the
                    # check: every workspace dependency must map to a layer.
                    err(
                        f"{name}: workspace dependency '{tname}' is not in an "
                        f"architectural layer; every workspace dependency must "
                        f"map to a layer"
                    )

    # --- Check 3: no crate dev-depends on test-support if test-support can
    # reach that crate back through the workspace graph (cargo cannot resolve
    # the resulting package cycle). The realistic case: `application` as a
    # dev-dependency of contracts/fakes that `application` itself consumes.
    for x_pid in workspace_members:
        x_name = packages[x_pid]["name"]
        if is_test_support(x_name):
            continue
        for ts_pid in all_edges.get(x_pid, []):
            ts_name = packages[ts_pid]["name"]
            if not (is_test_support(ts_name) and any(
                d.get("name") == ts_name and d.get("kind") == "dev"
                for d in packages[x_pid]["dependencies"]
            )):
                continue
            # Reachability from ts back to x over all workspace edges.
            seen: set = set()
            stack = list(all_edges.get(ts_pid, []))
            while stack:
                cur = stack.pop()
                if cur in seen:
                    continue
                seen.add(cur)
                if cur == x_pid:
                    err(
                        f"{x_name} <-> test-support dev-dependency cycle "
                        f"detected ({x_name} -> {ts_name} -> ... -> {x_name}); "
                        f"move cross-crate contract suites into adapter "
                        f"integration tests"
                    )
                    break
                stack.extend(all_edges.get(cur, []))
            if x_pid in seen:
                break

    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description="rusty-notes dependency-boundary checks (F9)")
    ap.add_argument("--metadata", help="path to pre-captured cargo metadata JSON")
    args = ap.parse_args()

    md = run_cargo_metadata(args.metadata)
    packages = {p["id"]: p for p in md.get("packages", [])}
    errors = find_violations(md)

    members = [packages[p]["name"] for p in md.get("workspace_members", []) if p in packages]
    print(f"arch-check: workspace members: {', '.join(members)}")
    print(
        "arch-check: rules: inward-only edges; test-support/fake/contract crates "
        "excluded from the production graph; no application<->test-support dev-cycle"
    )
    if errors:
        for e in errors:
            print(e)
        print(f"arch-check: FAILED with {len(errors)} violation(s)")
        return 1
    print("arch-check: OK — all dependency boundaries conform to docs/architecture-target.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
