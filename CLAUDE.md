# CLAUDE.md

## Project Overview

PlotJuggler Core — C++20 foundation in three modules:

- **pj_base** — vocabulary types (zero external deps)
- **pj_datastore** — columnar storage engine (Abseil, nanoarrow)
- **pj_plugins** — plugin protocol and host-side APIs (Qt 6.8.3 optional)

Dependency graph: `pj_datastore` → `pj_base`, `pj_plugins` → `pj_base` (independent of each other).

For architecture details see `docs/cpp_design_recommendations.md` and `pj_datastore/docs/`.

## Build & Test

```bash
./build.sh            # RelWithDebInfo (build/)
./build.sh --debug    # Debug + ASAN (build/debug_asan)
./test.sh             # runs tests in all discovered build dirs
```

Dependencies: Conan (`conanfile.txt`). Qt 6.8.3 optional (`./install_qt6.sh`).

## Pre-commit Validation

Before committing, always run:

```bash
./build.sh --debug && ./test.sh && ./run_clang_tidy.sh
```

## Coding Conventions

- **Formatting:** Google style via `.clang-format` — 2-space indent, 120-char limit
- **Naming:** `CamelCase` classes, `lower_case` functions/variables, `lower_case_` members, `kCamelCase` constants
- **Namespaces:** flat `PJ` namespace; `PJ::encoding` and `PJ::arrow_import` for internals
- **Errors:** `PJ::Expected<T>` for fallible ops, `PJ_ASSERT(cond, msg)` for invariants
- **Warnings:** `-Wall -Wextra -Werror` on all targets; pre-commit hooks enforce clang-format v17
