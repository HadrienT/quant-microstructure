#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Do not run this while another build is running on the server: the timings
# would be worthless.
NPROC="${NPROC:-16}"

cmake --preset bench
cmake --build build-bench --parallel "$NPROC" --target quantMicrostructure_bench
build-bench/benchmarks/quantMicrostructure_bench "$@"
