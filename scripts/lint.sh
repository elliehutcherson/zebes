#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/dev"
RUN_ALL=false
STRICT=false
FILES=()

Usage() {
  cat >&2 <<'EOF'
Usage: scripts/lint.sh [--strict] <source.cc> [source.cc ...]
       scripts/lint.sh [--strict] --all

Lint edited translation units locally. Header changes should be checked through
representative .cc files that include them. --all is reserved for CI and
occasional cleanup milestones because it is intentionally expensive.
EOF
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --all)
      RUN_ALL=true
      ;;
    --strict)
      STRICT=true
      ;;
    --help|-h)
      Usage
      ;;
    --*)
      echo "Unknown option: $1" >&2
      Usage
      ;;
    *)
      FILES+=("$1")
      ;;
  esac
  shift
done

if [[ "${RUN_ALL}" == true && ${#FILES[@]} -gt 0 ]]; then
  echo "Pass source files or --all, not both." >&2
  exit 2
fi

if [[ "${RUN_ALL}" == false && ${#FILES[@]} -eq 0 ]]; then
  Usage
fi

FindClangTidy() {
  if [[ -n "${CLANG_TIDY:-}" && -x "${CLANG_TIDY}" ]]; then
    printf '%s\n' "${CLANG_TIDY}"
    return
  fi

  if command -v clang-tidy >/dev/null 2>&1; then
    command -v clang-tidy
    return
  fi

  local llvm_prefix=""
  if command -v brew >/dev/null 2>&1; then
    llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
  fi

  for candidate in \
    "${llvm_prefix:+${llvm_prefix}/bin/clang-tidy}" \
    /usr/local/opt/llvm/bin/clang-tidy \
    /opt/homebrew/opt/llvm/bin/clang-tidy; do
    if [[ -n "${candidate}" && -x "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return
    fi
  done

  echo "clang-tidy was not found. Install LLVM or set CLANG_TIDY to its path." >&2
  exit 1
}

CLANG_TIDY_BIN="$(FindClangTidy)"
CLANG_TIDY_DIR="$(dirname "${CLANG_TIDY_BIN}")"

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  cmake --preset dev -S "${PROJECT_ROOT}"
fi

TIDY_ARGS=(-p "${BUILD_DIR}" -quiet)
if [[ "${STRICT}" == true ]]; then
  TIDY_ARGS+=('-warnings-as-errors=*')
fi

if [[ "$(uname -s)" == Darwin ]]; then
  if ! command -v xcrun >/dev/null 2>&1; then
    echo "xcrun is required to locate the macOS SDK." >&2
    exit 1
  fi
  SDK_PATH="$(xcrun --show-sdk-path)"
  TIDY_ARGS+=("-extra-arg=-isysroot" "-extra-arg=${SDK_PATH}")
fi

if [[ "${RUN_ALL}" == true ]]; then
  RUN_CLANG_TIDY="${CLANG_TIDY_DIR}/run-clang-tidy"
  if [[ ! -x "${RUN_CLANG_TIDY}" ]]; then
    echo "run-clang-tidy was not found beside ${CLANG_TIDY_BIN}." >&2
    exit 1
  fi

  LINT_JOBS="${LINT_JOBS:-2}"
  if [[ ! "${LINT_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "LINT_JOBS must be a positive integer." >&2
    exit 2
  fi

  SOURCE_REGEX="^${PROJECT_ROOT}/(src|tests)/.*\\.(c|cc|cpp|cxx)$"
  PATH="${CLANG_TIDY_DIR}:${PATH}" "${RUN_CLANG_TIDY}" \
    "${TIDY_ARGS[@]}" \
    -j "${LINT_JOBS}" \
    "${SOURCE_REGEX}"
  exit
fi

CANONICAL_FILES=()
for file in "${FILES[@]}"; do
  if [[ ! -f "${file}" ]]; then
    echo "Source file does not exist: ${file}" >&2
    exit 2
  fi

  canonical_file="$(cd "$(dirname "${file}")" && pwd -P)/$(basename "${file}")"
  case "${canonical_file}" in
    "${PROJECT_ROOT}"/src/*.c|"${PROJECT_ROOT}"/src/*.cc|"${PROJECT_ROOT}"/src/*.cpp|"${PROJECT_ROOT}"/src/*.cxx|\
    "${PROJECT_ROOT}"/tests/*.c|"${PROJECT_ROOT}"/tests/*.cc|"${PROJECT_ROOT}"/tests/*.cpp|"${PROJECT_ROOT}"/tests/*.cxx)
      CANONICAL_FILES+=("${canonical_file}")
      ;;
    "${PROJECT_ROOT}"/src/*.h|"${PROJECT_ROOT}"/src/*.hpp|"${PROJECT_ROOT}"/tests/*.h|"${PROJECT_ROOT}"/tests/*.hpp)
      echo "Headers have no standalone compile command: ${file}" >&2
      echo "Pass one or more representative .cc files that include this header." >&2
      exit 2
      ;;
    *)
      echo "Only C++ translation units under src/ and tests/ may be linted: ${file}" >&2
      exit 2
      ;;
  esac
done

RunScopedBatch() {
  local files=("$@")
  local logs=()
  local pids=()
  local command_status=0
  local file=""
  local index=0
  local log_file=""
  local status=0

  for file in "${files[@]}"; do
    log_file="$(mktemp "${TMPDIR:-/tmp}/zebes-clang-tidy.XXXXXX")"
    logs+=("${log_file}")
    "${CLANG_TIDY_BIN}" "${TIDY_ARGS[@]}" "${file}" \
      >"${log_file}" 2>&1 &
    pids+=("$!")
  done

  for ((index = 0; index < ${#files[@]}; ++index)); do
    if wait "${pids[index]}"; then
      echo "PASS clang-tidy ${files[index]#"${PROJECT_ROOT}/"}"
    else
      command_status=$?
      if [[ ${status} -eq 0 ]]; then
        status=${command_status}
      fi
      echo "FAIL clang-tidy ${files[index]#"${PROJECT_ROOT}/"}" >&2
      cat "${logs[index]}" >&2
    fi
    rm -f "${logs[index]}"
  done

  return "${status}"
}

lint_status=0
for ((batch_start = 0; batch_start < ${#CANONICAL_FILES[@]}; batch_start += 2)); do
  batch=("${CANONICAL_FILES[@]:batch_start:2}")
  RunScopedBatch "${batch[@]}" || lint_status=$?
done
exit "${lint_status}"
