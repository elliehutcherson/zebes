#!/bin/bash
set -e  # Exit immediately if any command fails

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_TESTS=true
TEST_FILTER=""
PRESET="dev"

while [[ $# -gt 0 ]]; do
  case $1 in
    --no-tests|--no_tests)
      RUN_TESTS=false
      shift
      ;;
    --ui-tests|--ui_tests)
      PRESET="ui"
      shift
      ;;
    --test_filter|--test-filter)
      TEST_FILTER="$2"
      shift
      shift
      ;;
    --test_filter=*|--test-filter=*)
      TEST_FILTER="${1#*=}"
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

echo "=========================================="
echo "  ZEBES: Build & Test Sanity Check"
echo "=========================================="

echo "[1/3] Configuring CMake..."
cmake --preset "${PRESET}" -S "${PROJECT_ROOT}"

echo "[2/3] Building..."
cmake --build --preset "${PRESET}"

# 3. Test
if [ "$RUN_TESTS" = true ]; then
    echo "[3/3] Running Tests..."
    if [ -n "$TEST_FILTER" ]; then
        ctest --preset "${PRESET}" -R "${TEST_FILTER}"
    else
        ctest --preset "${PRESET}"
    fi
    echo "Running build_cleaner integration test..."
    python3 tests/build_cleaner_test.py
else
    echo "[3/3] Skipping Tests (--no-tests flag provided)"
fi

echo "=========================================="
echo "  SUCCESS: All targets built."
echo "=========================================="
