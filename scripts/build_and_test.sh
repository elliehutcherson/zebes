#!/bin/bash
set -e  # Exit immediately if any command fails

# Get the project root directory (one level up from this script)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"


# 0. Parse Arguments
# 0. Parse Arguments
RUN_TESTS=true
TEST_FILTER=""

while [[ $# -gt 0 ]]; do
  case $1 in
    --no-tests|--no_tests)
      RUN_TESTS=false
      shift # past argument
      ;;
    --test_filter|--test-filter)
      TEST_FILTER="$2"
      shift # past argument
      shift # past value
      ;;
    --test_filter=*|--test-filter=*)
      TEST_FILTER="${1#*=}"
      shift # past argument=value
      ;;
    *)
      # Build directory check or others can go here if needed
      shift # past argument
      ;;
  esac
done

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
if [ "$RUN_TESTS" = true ]; then
    # --output-on-failure helps you immediately see why a test failed.
    echo "[3/3] Running Tests..."
    # --output-on-failure helps you immediately see why a test failed.
    echo "[3/3] Running Tests..."
    CTEST_ARGS="--test-dir ${BUILD_DIR} --output-on-failure"
    if [ -n "$TEST_FILTER" ]; then
        ctest $CTEST_ARGS -R "${TEST_FILTER}"
    else
        ctest $CTEST_ARGS
    fi
    echo "Running build_cleaner integration test..."
    python3 tests/build_cleaner_test.py
else
    echo "[3/3] Skipping Tests (--no-tests flag provided)"
fi

echo "=========================================="
echo "  SUCCESS: All targets built."
echo "=========================================="