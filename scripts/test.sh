#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="dev"

Usage() {
  cat >&2 <<'EOF'
Usage: scripts/test.sh [--ui] <test-target> [gtest-filter]
       scripts/test.sh [--ui] --affected-target <cmake-target>
       scripts/test.sh --list

Build and run one C++ test executable, or every test that depends on a CMake
target. Examples:
  scripts/test.sh --list
  scripts/test.sh terrain_generator_test
  scripts/test.sh terrain_generator_test TerrainGeneratorTest.EverySlopeShapeRenders
  scripts/test.sh --affected-target background_task
  scripts/test.sh --ui sanity_test
EOF
  exit 2
}

ListTargets() {
  find "${PROJECT_ROOT}/tests" -type f -name CMakeLists.txt -print0 |
    while IFS= read -r -d '' cmake_file; do
      sed -nE \
        -e 's/^[[:space:]]*gtest_discover_tests\(([a-zA-Z0-9_+-]+).*/\1/p' \
        -e 's/^[[:space:]]*add_test\(NAME[[:space:]]+([a-zA-Z0-9_+-]+).*/\1/p' \
        "${cmake_file}"
    done |
    sort -u
}

if [[ "${1:-}" == "--list" ]]; then
  [[ $# -eq 1 ]] || Usage
  ListTargets
  exit 0
fi

if [[ "${1:-}" == "--ui" ]]; then
  PRESET="ui"
  shift
fi

BUILD_DIR="${PROJECT_ROOT}/build/${PRESET}"

if [[ "${1:-}" == "--affected-target" ]]; then
  [[ $# -eq 2 ]] || Usage
  AFFECTED_TARGET="$2"
  if [[ ! "${AFFECTED_TARGET}" =~ ^[a-zA-Z0-9_+.-]+$ ]]; then
    echo "Invalid CMake target: ${AFFECTED_TARGET}" >&2
    exit 2
  fi

  QUERY_DIR="${BUILD_DIR}/.cmake/api/v1/query/client-zebes"
  REPLY_DIR="${BUILD_DIR}/.cmake/api/v1/reply"
  mkdir -p "${QUERY_DIR}"
  touch "${QUERY_DIR}/codemodel-v2"

  AFFECTED_FILE="$(mktemp "${TMPDIR:-/tmp}/zebes-affected-tests.XXXXXX")"
  COMMAND_OUTPUT="$(mktemp "${TMPDIR:-/tmp}/zebes-test-output.XXXXXX")"
  trap 'rm -f "${AFFECTED_FILE}" "${COMMAND_OUTPUT}"' EXIT

  if cmake --preset "${PRESET}" -S "${PROJECT_ROOT}" \
      >"${COMMAND_OUTPUT}" 2>&1; then
    echo "Configured ${PRESET} test build."
  else
    status=$?
    echo "Failed to configure ${PRESET} test build:" >&2
    cat "${COMMAND_OUTPUT}" >&2
    exit "${status}"
  fi

  python3 "${PROJECT_ROOT}/scripts/affected_tests.py" \
    --reply-dir "${REPLY_DIR}" "${AFFECTED_TARGET}" >"${AFFECTED_FILE}"

  AFFECTED_TESTS=()
  while IFS= read -r test_target; do
    [[ -n "${test_target}" ]] || continue
    if [[ ! "${test_target}" =~ ^[a-zA-Z0-9_+-]+$ ]]; then
      echo "Invalid affected test target: ${test_target}" >&2
      exit 1
    fi
    AFFECTED_TESTS+=("${test_target}")
  done <"${AFFECTED_FILE}"

  echo "Affected tests for ${AFFECTED_TARGET}:"
  for test_target in "${AFFECTED_TESTS[@]}"; do
    echo "  ${test_target}"
  done

  build_started=${SECONDS}
  if cmake --build --preset "${PRESET}" \
      --target "${AFFECTED_TESTS[@]}" >"${COMMAND_OUTPUT}" 2>&1; then
    echo "Built ${#AFFECTED_TESTS[@]} affected test targets in $((SECONDS - build_started))s."
  else
    status=$?
    echo "Failed to build affected tests for ${AFFECTED_TARGET}:" >&2
    cat "${COMMAND_OUTPUT}" >&2
    exit "${status}"
  fi

  test_started=${SECONDS}
  for test_target in "${AFFECTED_TESTS[@]}"; do
    TEST_BINARY="${BUILD_DIR}/bin/tests/${test_target}"
    if [[ ! -x "${TEST_BINARY}" ]]; then
      echo "Target did not produce a test executable at ${TEST_BINARY}" >&2
      exit 1
    fi

    if "${TEST_BINARY}" >"${COMMAND_OUTPUT}" 2>&1; then
      echo "PASS ${test_target}"
    else
      status=$?
      echo "FAIL ${test_target}" >&2
      cat "${COMMAND_OUTPUT}" >&2
      exit "${status}"
    fi
  done
  echo "Passed ${#AFFECTED_TESTS[@]} affected test executables in $((SECONDS - test_started))s."
  exit 0
fi

[[ $# -ge 1 && $# -le 2 ]] || Usage

TEST_TARGET="$1"
GTEST_FILTER="${2:-}"

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
