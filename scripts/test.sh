#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="dev"

Usage() {
  cat >&2 <<'EOF'
Usage: scripts/test.sh [--ui] <test-target> [gtest-filter]

Build and run one C++ test executable. Examples:
  scripts/test.sh terrain_generator_test
  scripts/test.sh terrain_generator_test TerrainGeneratorTest.EverySlopeShapeRenders
  scripts/test.sh --ui sanity_test
EOF
  exit 2
}

if [[ "${1:-}" == "--ui" ]]; then
  PRESET="ui"
  shift
fi

[[ $# -ge 1 && $# -le 2 ]] || Usage

TEST_TARGET="$1"
GTEST_FILTER="${2:-}"
BUILD_DIR="${PROJECT_ROOT}/build/${PRESET}"

if [[ ! "${TEST_TARGET}" =~ ^[a-zA-Z0-9_+-]+$ ]]; then
  echo "Invalid CMake test target: ${TEST_TARGET}" >&2
  exit 2
fi

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cmake --preset "${PRESET}" -S "${PROJECT_ROOT}"
fi

cmake --build --preset "${PRESET}" --target "${TEST_TARGET}"

TEST_BINARY="${BUILD_DIR}/bin/tests/${TEST_TARGET}"
if [[ ! -x "${TEST_BINARY}" ]]; then
  echo "Target did not produce a test executable at ${TEST_BINARY}" >&2
  exit 1
fi

if [[ -n "${GTEST_FILTER}" ]]; then
  "${TEST_BINARY}" "--gtest_filter=${GTEST_FILTER}"
else
  "${TEST_BINARY}"
fi
