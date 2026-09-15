# Developer setup (Linux Notes)

## Tested toolchain (delivery container)

| Component | Version / path |
|-----------|----------------|
| g++ | 14.2.0 |
| CMake | 3.30.5 (official binary) |
| Ninja | 1.12.1 |
| Qt | 6.7.3 `linux_gcc_64` via [aqtinstall](https://github.com/miurahr/aqtinstall) |
| SQLite | system `libsqlite3` 3.46.x |
| Catch2 | 3.x amalgamated under `third_party/catch2` |

System package install may require root. Without root, use user-local CMake/Ninja/Qt and extract OpenGL/xkb/dbus runtime debs into a prefix (see delivery evidence `env/env.sh` pattern).

## Configure & build

```bash
export CMAKE_PREFIX_PATH=/path/to/Qt/6.7.3/gcc_64
export QT_QPA_PLATFORM=offscreen   # headless
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNOTES_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizers:

```bash
cmake -G Ninja -S . -B build-san -DNOTES_ENABLE_SANITIZERS=ON
cmake --build build-san
```

## Run

```bash
export QT_QPA_PLATFORM=xcb   # or wayland; offscreen for smoke only
./build/notes
```

Data directory: `$XDG_DATA_HOME/linux-notes` (default `~/.local/share/linux-notes`) with `notes.db` and `attachments/`.

## Architecture check

```bash
bash scripts/architecture_check.sh
```

## CI jobs

GitHub Actions workflow defines jobs named **`test`** and **`architecture-check`**.
