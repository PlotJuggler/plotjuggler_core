# Plan: Reorganize Project into Three Modules

## Context

The project currently has a flat `data/` directory containing both the base vocabulary library
and the storage engine, with tests/benchmarks/examples in shared subdirectories. The `plugins/`
directory is separate. We want to reorganize into three clearly separated modules (`pj_base`,
`pj_datastore`, `pj_plugins`), each self-contained with their own `docs/`, `tests/`, and where
appropriate `benchmarks/` and `examples/`.

**Build approach**: Top-level `CMakeLists.txt` with `add_subdirectory()` per module.
Single `conanfile.txt` for external deps. Convenience `build.sh` and `test.sh` scripts.

**Dependency graph**: `pj_datastore` → `pj_base`, `pj_plugins` → `pj_base`.

## Target Layout

```
plotjuggler_core/
├── CMakeLists.txt              # NEW — top-level project
├── conanfile.txt               # MOVED from data/
├── build.sh                    # NEW — conan install + cmake configure + build
├── test.sh                     # NEW — ctest wrapper
├── CLAUDE.md                   # UPDATED
├── docs/
│   ├── cpp_design_recommendations.md
│   └── plans/
│       └── 2026-03-03-project-reorganization.md
├── pj_base/
│   ├── CMakeLists.txt
│   ├── include/pj_base/        # 6 headers  (#include "pj_base/types.hpp")
│   ├── src/
│   ├── tests/                  # 4 tests
│   └── docs/
├── pj_datastore/
│   ├── CMakeLists.txt
│   ├── include/pj_datastore/   # 15 headers (#include "pj_datastore/engine.hpp")
│   ├── src/                    # 13 source files
│   ├── tests/                  # 11 tests
│   ├── benchmarks/             # 2 benchmarks
│   ├── examples/               # parquet_import.cpp
│   └── docs/                   # 4 .md files from data/
├── pj_plugins/
│   ├── CMakeLists.txt          # add_subdirectory(dialog_protocol)
│   ├── dialog_protocol/
│   │   ├── CMakeLists.txt
│   │   ├── include/pj_plugins/ # headers (#include "pj_plugins/dialog_protocol.h")
│   │   │   ├── dialog_protocol.h
│   │   │   ├── sdk/            # widget_data.hpp, etc.
│   │   │   ├── host/           # dialog_handle.hpp, etc.
│   │   │   └── host_qt/        # dialog_engine.hpp, etc.
│   │   ├── src/
│   │   ├── tests/
│   │   └── examples/
│   └── docs/                   # plotjuggler-plugin-plan.md
```

## Renames Summary

| What | Before | After | Scope |
|------|--------|-------|-------|
| Include prefix | `PJ/base/` | `pj_base/` | ~20 files (base + engine + tests) |
| Include prefix | `PJ/engine/` | `pj_datastore/` | ~40 files |
| Include prefix | `PJ/sdk/`, `PJ/host/`, `PJ/host_qt/`, `PJ/dialog_protocol.h` | `pj_plugins/sdk/`, `pj_plugins/host/`, `pj_plugins/host_qt/`, `pj_plugins/dialog_protocol.h` | ~20 plugin files |
| Namespace | `PJ::base`, `PJ::engine`, `PJ::sdk`, `PJ::host`, `PJ::host_qt` | all flattened to `PJ` | ~51 files, ~100 occurrences |
| Qualified refs | `PJ::engine::Foo`, `PJ::sdk::Foo`, `PJ::host::Foo`, `PJ::host_qt::Foo` | `PJ::Foo` | ~22 files |
| CMake target | `plotjuggler_base` | `pj_base` | CMake files |
| CMake target | `plotjuggler_engine` | `pj_datastore` | CMake files |
| CMake project | `pj_engine` | `plotjuggler_core` | top-level |

Note: `PJ::base::` is never used as a qualified reference — base types are already `PJ::Timestamp` etc.

## Steps

### 1. Create directory structure + git mv files

```bash
# --- pj_base ---
mkdir -p pj_base/{include,src,tests,docs}
git mv data/base/include/PJ/base       pj_base/include/pj_base       # RENAME
git mv data/base/src/type_tree.cpp      pj_base/src/
git mv data/tests/types_test.cpp        pj_base/tests/
git mv data/tests/type_tree_test.cpp    pj_base/tests/
git mv data/tests/span_test.cpp         pj_base/tests/
git mv data/tests/expected_test.cpp     pj_base/tests/

# --- pj_datastore ---
mkdir -p pj_datastore/{include,src,tests,benchmarks,examples,docs}
git mv data/engine/include/PJ/engine    pj_datastore/include/pj_datastore  # RENAME
git mv data/engine/src/*.cpp            pj_datastore/src/
for f in type_registry_test buffer_test column_buffer_test encoding_test \
         chunk_test topic_storage_test query_test engine_integration_test \
         derived_engine_test array_expansion_test arrow_import_test; do
  git mv data/tests/${f}.cpp pj_datastore/tests/
done
git mv data/benchmarks/*               pj_datastore/benchmarks/
git mv data/examples/*                 pj_datastore/examples/
git mv data/architecture_overview.md               pj_datastore/docs/
git mv data/data_implementation_plan.md            pj_datastore/docs/
git mv data/dag-plan.md                            pj_datastore/docs/
git mv data/plotjuggler_siso_mimo_analysis.md      pj_datastore/docs/

# --- pj_plugins ---
mkdir -p pj_plugins/dialog_protocol/include
git mv plugins/dialog_protocol/include/PJ  pj_plugins/dialog_protocol/include/pj_plugins  # RENAME
git mv plugins/dialog_protocol/src         pj_plugins/dialog_protocol/src
git mv plugins/dialog_protocol/tests       pj_plugins/dialog_protocol/tests
git mv plugins/dialog_protocol/examples    pj_plugins/dialog_protocol/examples
git mv plugins/dialog_protocol/CMakeLists.txt pj_plugins/dialog_protocol/
git mv plugins/docs                        pj_plugins/docs

# --- Top-level ---
git mv data/conanfile.txt               conanfile.txt

# --- Clean up ---
git rm data/CMakeLists.txt
git rm data/run_benchmark.sh
rm -f data/CMakeUserPresets.json
```

### 2. Rename include paths

**pj_base headers** (`pj_base/include/pj_base/`):
- `"PJ/base/` → `"pj_base/` in all base headers (self-references)

**pj_datastore** (headers, sources, tests, benchmarks, examples — ~40 files):
- `"PJ/engine/` → `"pj_datastore/`
- `<PJ/engine/` → `<pj_datastore/`
- `"PJ/base/` → `"pj_base/` (cross-module references)
- `<PJ/base/` → `<pj_base/`

**pj_plugins** (headers, sources, tests, examples — ~20 files):
- `<PJ/sdk/` → `<pj_plugins/sdk/`
- `<PJ/host/` → `<pj_plugins/host/`
- `<PJ/host_qt/` → `<pj_plugins/host_qt/`
- `<PJ/dialog_protocol.h>` → `<pj_plugins/dialog_protocol.h>`
- `"PJ/dialog_protocol.h"` → `"pj_plugins/dialog_protocol.h"`

### 3. Flatten namespaces to `PJ`

In all ~51 files:
- `namespace PJ::base {` → `namespace PJ {`
- `namespace PJ::engine {` → `namespace PJ {`
- `namespace PJ::sdk {` → `namespace PJ {`
- `namespace PJ::host {` → `namespace PJ {`
- `namespace PJ::host_qt {` → `namespace PJ {`

Qualified references (~22 files):
- `PJ::engine::Foo` → `PJ::Foo`
- `PJ::sdk::Foo` → `PJ::Foo`
- `PJ::host::Foo` → `PJ::Foo`
- `PJ::host_qt::Foo` → `PJ::Foo`

(`PJ::base::` is never used — no changes needed there.)

### 4. Write CMakeLists.txt files

**Top-level `CMakeLists.txt`** (NEW):
- `project(plotjuggler_core LANGUAGES CXX)`, C++20
- Options: `PJ_ASSERT_THROWS`, `PJ_ENABLE_SANITIZERS`, `PJ_BUILD_PARQUET_IMPORT_EXAMPLE`,
  `PJ_BUILD_DIALOG_ENGINE_QT`
- Sanitizer flags
- `find_package()` for absl, GTest, benchmark
- Nanoarrow detection logic (~60 lines from current data/CMakeLists.txt)
- `enable_testing()`
- `add_subdirectory(pj_base)`, `add_subdirectory(pj_datastore)`, `add_subdirectory(pj_plugins)`

**`pj_base/CMakeLists.txt`**:
- `add_library(pj_base STATIC src/type_tree.cpp)`
- `target_include_directories(pj_base PUBLIC include)`
- `target_compile_features(pj_base PUBLIC cxx_std_20)`
- Warning flags, `PJ_ASSERT_THROWS` definition
- 4 test executables linking `pj_base` + `GTest::gtest_main`

**`pj_datastore/CMakeLists.txt`**:
- `add_library(pj_datastore STATIC ...)` (13 source files)
- `target_include_directories(pj_datastore PUBLIC include)`
- `target_link_libraries(pj_datastore PUBLIC pj_base absl::flat_hash_map absl::hash
  PRIVATE absl::strings ${nanoarrow})`
- Warning flags, `PJ_ASSERT_THROWS` definition
- 10 engine tests + `arrow_import_test`
- 2 benchmark executables
- Optional parquet_import example

**`pj_plugins/CMakeLists.txt`**:
- `add_subdirectory(dialog_protocol)`

**`pj_plugins/dialog_protocol/CMakeLists.txt`** (updated):
- Update include paths from `PJ/` to `pj_plugins/`
- Keep existing structure (own project, FetchContent for nlohmann_json + gtest)

### 5. Write build.sh and test.sh

**`build.sh`**:
```bash
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="${1:-Release}"

conan install "$SCRIPT_DIR" --output-folder="$BUILD_DIR" --build=missing
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j "$(nproc)"
```

**`test.sh`**:
```bash
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "ERROR: build directory not found. Run ./build.sh first."
  exit 1
fi

cd "$BUILD_DIR" && ctest --output-on-failure "$@"
```

### 6. Update .gitignore

Replace old `data/` and `plugins/` patterns with:
```
build/
CMakeUserPresets.json
pj_datastore/.cache/
pj_datastore/test_data/
pj_datastore/benchmark_reports/
pj_plugins/dialog_protocol/build*
```

### 7. Update CLAUDE.md

- All `data/` paths → new module paths
- `PJ/engine/` → `pj_datastore/`, `PJ/base/` → `pj_base/`
- `PJ::engine`, `PJ::base` → `PJ`
- `plotjuggler_base` → `pj_base`, `plotjuggler_engine` → `pj_datastore`
- Build commands: `./build.sh` and `./test.sh`

### 8. Update documentation .md files

- `pj_datastore/docs/architecture_overview.md` — update paths + terminology
- `pj_datastore/docs/data_implementation_plan.md` — update paths + terminology
- `pj_plugins/docs/plotjuggler-plugin-plan.md` — update `data/engine/`, `PJ::engine` refs
- `docs/cpp_design_recommendations.md` — check for stale paths

### 9. Update .vscode configs

- `.vscode/c_cpp_properties.json` — update include paths
- `.vscode/settings.json` — update if referencing old paths

### 10. Add GitHub Actions CI workflow

Create `.github/workflows/ci.yml` for Ubuntu 22.04, using the official
[`conan-io/setup-conan`](https://github.com/conan-io/setup-conan) action:

```yaml
name: CI
on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - uses: conan-io/setup-conan@v1
        with:
          cache_packages: true     # cache ~/.conan2 packages across runs

      - name: Build
        run: ./build.sh

      - name: Test
        run: ./test.sh
```

The `conan-io/setup-conan@v1` action handles:
- Installing the latest Conan 2.x
- Auto-detecting a profile matching the runner (`profile_detect: true` by default)
- Caching Conan packages via GitHub cache when `cache_packages: true`

Note: Ubuntu 22.04 ships GCC 11 which has ASAN/UBSan compatibility issues with abseil
(documented in the existing CMakeLists.txt). The CI workflow uses a plain Release build
without sanitizers. GCC 13+ can be added as a separate matrix entry if desired.

## Verification

1. **Build**: `./build.sh`
2. **Tests**: `./test.sh` — all 15 datastore tests + 8 dialog protocol tests pass
3. **No stale include paths**: `grep -r "PJ/base/\|PJ/engine/\|PJ/sdk/\|PJ/host/" --include='*.cpp' --include='*.hpp' --include='*.h'` → nothing
4. **No stale namespaces**: `grep -r "PJ::engine\|PJ::base\|PJ::sdk\|PJ::host" --include='*.cpp' --include='*.hpp' --include='*.h'` → nothing
5. **No stale targets**: `grep -r "plotjuggler_base\|plotjuggler_engine" --include='*.txt' --include='*.cpp' --include='*.hpp'` → nothing
6. **No stale dir refs**: `grep -r "data/base\|data/engine\|data/tests\|data/benchmarks" --include='*.cpp' --include='*.hpp' --include='*.txt' --include='*.md'` → nothing
7. **Git clean**: no untracked files in old `data/` or `plugins/` dirs
