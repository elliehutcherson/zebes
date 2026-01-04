#!/bin/bash
set -e  # Exit immediately if any command fails

# Get the project root directory (one level up from this script)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "=========================================="
echo "  ZEBES: Build & Test Sanity Check"
echo "=========================================="

# 1. Configure
# We force Debug mode to ensure assertions and debug symbols are active.
echo "[1/3] Configuring CMake..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTING=ON

# 2. Build
# -j$(nproc) uses all available CPU cores for faster compilation.
echo "[2/3] Building..."
cmake --build "${BUILD_DIR}" -j$(nproc)

# 3. Test
# --output-on-failure helps you immediately see why a test failed.
echo "[3/3] Running Tests..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "=========================================="
echo "  SUCCESS: All targets built & tests passed."
echo "=========================================="