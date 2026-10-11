#!/usr/bin/env bash
#
# Builds the `quantmicrostructure` wheel into dist/.
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# The library has no third-party dependency, so the wheel builds without the
# vcpkg toolchain. The day it gains one, pass the toolchain file here through
# CMAKE_ARGS (pyproject.toml cannot reference $VCPKG_ROOT).

# uv if present (a uv-created environment has no pip), otherwise pip + build.
if command -v uv >/dev/null 2>&1; then
  uv build --wheel --out-dir dist
else
  python -m pip install --upgrade pip build
  python -m build --wheel
fi

echo "Wheel built in dist/"
