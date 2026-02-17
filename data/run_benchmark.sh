#!/usr/bin/env bash
# Run benchmarks and save a timestamped report for comparison.
# Usage: ./run_benchmark.sh [extra benchmark flags...]
#
# Reports are saved to data/benchmark_reports/<timestamp>.txt
# The latest report is also symlinked as data/benchmark_reports/latest.txt

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
REPORT_DIR="${SCRIPT_DIR}/benchmark_reports"
BENCHMARK_BIN="${BUILD_DIR}/read_benchmark"

# Ensure the benchmark binary exists
if [[ ! -x "${BENCHMARK_BIN}" ]]; then
  echo "ERROR: benchmark binary not found at ${BENCHMARK_BIN}"
  echo "       Run 'cd build && make -j\$(nproc)' first."
  exit 1
fi

mkdir -p "${REPORT_DIR}"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
REPORT_FILE="${REPORT_DIR}/${TIMESTAMP}.txt"

{
  echo "=== Benchmark Report ==="
  echo "Date: $(date -Iseconds)"
  echo "Host: $(hostname)"
  echo "CPU:  $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)"
  echo "Git:  $(git -C "${SCRIPT_DIR}" rev-parse --short HEAD 2>/dev/null || echo 'n/a')"
  echo "Branch: $(git -C "${SCRIPT_DIR}" branch --show-current 2>/dev/null || echo 'n/a')"
  echo ""
  "${BENCHMARK_BIN}" --benchmark_format=console "$@" 2>&1
} | tee "${REPORT_FILE}"

# Update the 'latest' symlink
ln -sf "${TIMESTAMP}.txt" "${REPORT_DIR}/latest.txt"

echo ""
echo "Report saved to: ${REPORT_FILE}"
echo "Latest symlink:  ${REPORT_DIR}/latest.txt"
