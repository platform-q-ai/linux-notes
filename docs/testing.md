# Testing

## Layout
- `tests/domain` — pure C++ domain tests
- `tests/application/use_cases` — use cases + `tests/support` fakes
- `tests/adapters` — adapter unit tests when needed
- `tests/contracts` — **same** note-store suite vs in-memory fake **and** SQLite
- `tests/presentation` — Qt/headless critical scenarios (`QT_QPA_PLATFORM=offscreen`)
- `tests/support/fakes` — production targets must not link these

## Critical scenarios
1. Note switch during debounce (no cross-note write)
2. Stale async completion ignored (generation token)
3. Save error keeps dirty + error state
4. Save then reopen persistence (fake + sqlite)
5. Deletion ordering with pending save / folder children
6. Attachment/content roundtrip when feature is enabled

## Run (headless)
```bash
source delivery-evidence/env/env.sh   # or export Qt/CMake PATH locally
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DNOTES_BUILD_TESTS=ON
cmake --build build --target test_notes_domain test_notes_save_note test_notes_store_contract
QT_QPA_PLATFORM=offscreen LC_ALL=C.UTF-8 ctest --test-dir build --output-on-failure
```

## Sanitizers
```bash
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DNOTES_BUILD_TESTS=ON \
  -DNOTES_ENABLE_SANITIZERS=ON \
  -DNOTES_BUILD_UI=OFF
cmake --build build-asan
QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build-asan --output-on-failure
```

## Headless vs visual smoke
- **Headless / automated:** `ctest` and binaries above with offscreen Qt.
- **Visual smoke:** manual GUI only; record under
  `delivery-evidence/tests/results/visual-smoke-*`. Never claim as CI green.

## Architecture check
```bash
bash scripts/architecture_check.sh
```
CI job name: `architecture-check` (preserve). Test job name: `test` (preserve).
