#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# If explicit build directories exist (from --debug), test each.
# Otherwise test the default build/ directory.

run_tests() {
  local build_dir="$1"
  local label="$2"

  if [[ ! -d "$build_dir" ]]; then
    echo "SKIP: ${label} — ${build_dir} not found"
    return 0
  fi

  echo ""
  echo "=== Testing: ${label} (${build_dir}) ==="
  echo ""
  cd "$build_dir" && ctest --output-on-failure "$@"
}

TESTED=0

for dir_label in \
  "build/debug_asan:Debug+ASAN" \
  "build:Default"; do

  dir="${dir_label%%:*}"
  label="${dir_label##*:}"
  full_path="${SCRIPT_DIR}/${dir}"

  if [[ -f "$full_path/CTestTestfile.cmake" ]]; then
    run_tests "$full_path" "$label"
    TESTED=$((TESTED + 1))
  fi
done

if [[ $TESTED -eq 0 ]]; then
  echo "ERROR: no build directory found. Run ./build.sh first."
  exit 1
fi
