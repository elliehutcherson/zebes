#!/bin/bash
set -e  # Exit immediately if any command fails

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_TESTS=true
TEST_FILTER=""
PRESET="dev"
TEST_PRESET="dev"
VENV_PYTHON="${PROJECT_ROOT}/build/tileset-venv/bin/python"

while [[ $# -gt 0 ]]; do
  case $1 in
    --no-tests|--no_tests)
      RUN_TESTS=false
      shift
      ;;
    --ui-tests|--ui_tests)
      PRESET="ui"
      TEST_PRESET="ui"
      shift
      ;;
    --all-tests-with-ui|--all_tests_with_ui)
      PRESET="ui"
      TEST_PRESET="ui-all"
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

# The rule files under .claude/ are generated from docs/style-guide.md. Check
# them before spending a build on a tree whose guide and rules disagree.
echo "[1/4] Checking generated rule files..."
python3 "${PROJECT_ROOT}/scripts/sync_rules.py" --check --root "${PROJECT_ROOT}"

echo "[2/4] Configuring CMake..."
cmake --preset "${PRESET}" -S "${PROJECT_ROOT}"

echo "[3/4] Building..."
cmake --build --preset "${PRESET}"

# 3. Test
if [ "$RUN_TESTS" = true ]; then
    echo "[4/4] Running Tests..."
    if [ -n "$TEST_FILTER" ]; then
        ctest --preset "${TEST_PRESET}" -R "${TEST_FILTER}"
    else
        ctest --preset "${TEST_PRESET}"
    fi
    # The asset-tool tests need numpy/Pillow, which live in an isolated venv
    # under build/. That directory is gitignored and is routinely deleted, so
    # create it on demand instead of failing an otherwise clean tree.
    if [ ! -x "${VENV_PYTHON}" ]; then
        echo "Creating Python environment for asset-tool tests..."
        python3 -m venv "${PROJECT_ROOT}/build/tileset-venv"
        "${VENV_PYTHON}" -m pip install --quiet --upgrade pip
        "${VENV_PYTHON}" -m pip install --quiet -r "${PROJECT_ROOT}/scripts/requirements-tileset.txt"
    fi

    echo "Running Python tool tests..."
    "${VENV_PYTHON}" -m unittest discover \
        --start-directory "${PROJECT_ROOT}/tests" \
        --pattern '*_test.py'
else
    echo "[4/4] Skipping Tests (--no-tests flag provided)"
fi

echo "=========================================="
echo "  SUCCESS: All targets built."
echo "=========================================="
