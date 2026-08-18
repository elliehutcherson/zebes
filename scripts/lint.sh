#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/dev"
RUN_ALL=false
STRICT=false
USE_CACHE=true
FILES=()

Usage() {
  cat >&2 <<'EOF'
Usage: scripts/lint.sh [--strict] [--no-cache] <source.cc> [source.cc ...]
       scripts/lint.sh [--strict] --all

Lint edited translation units locally. Header changes should be checked through
representative .cc files that include them. Successful scoped results are
cached; --no-cache forces fresh analysis. --all is reserved for CI and
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
    --no-cache)
      USE_CACHE=false
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

CACHE_KEYS=()
CACHE_DIR="${LINT_CACHE_DIR:-${BUILD_DIR}/.lint-cache}"
CACHE_HELPER="${LINT_CACHE_HELPER:-${PROJECT_ROOT}/scripts/lint_cache.py}"
if [[ "${USE_CACHE}" == true && -f "${CACHE_HELPER}" ]]; then
  CACHE_KEY_FILE="$(mktemp "${TMPDIR:-/tmp}/zebes-lint-cache-keys.XXXXXX")"
  cache_args=(
    --project-root "${PROJECT_ROOT}"
    --build-dir "${BUILD_DIR}"
    --clang-tidy "${CLANG_TIDY_BIN}"
    --state-dir "${CACHE_DIR}"
  )
  if [[ "${STRICT}" == true ]]; then
    cache_args+=(--strict)
  fi
  if [[ "$(uname -s)" == Darwin ]]; then
    cache_args+=("--extra-arg=-isysroot" "--extra-arg=${SDK_PATH}")
  fi
  if python3 "${CACHE_HELPER}" "${cache_args[@]}" "${CANONICAL_FILES[@]}" \
      >"${CACHE_KEY_FILE}"; then
    while IFS= read -r cache_key; do
      CACHE_KEYS+=("${cache_key}")
    done <"${CACHE_KEY_FILE}"
  fi
  rm -f "${CACHE_KEY_FILE}"
fi

if [[ ${#CACHE_KEYS[@]} -ne ${#CANONICAL_FILES[@]} ]]; then
  CACHE_KEYS=()
  for ((index = 0; index < ${#CANONICAL_FILES[@]}; ++index)); do
    CACHE_KEYS+=("-")
  done
fi
for ((index = 0; index < ${#CACHE_KEYS[@]}; ++index)); do
  if [[ ! "${CACHE_KEYS[index]}" =~ ^[0-9a-f]{64}$ ]]; then
    CACHE_KEYS[index]="-"
  fi
done

CacheKeyForFile() {
  local requested_file="$1"
  local index=0
  for ((index = 0; index < ${#CANONICAL_FILES[@]}; ++index)); do
    if [[ "${CANONICAL_FILES[index]}" == "${requested_file}" ]]; then
      printf '%s\n' "${CACHE_KEYS[index]}"
      return
    fi
  done
  printf '%s\n' '-'
}

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
      cache_key="$(CacheKeyForFile "${files[index]}")"
      if [[ "${cache_key}" != "-" ]]; then
        mkdir -p "${CACHE_DIR}"
        touch "${CACHE_DIR}/${cache_key}"
      fi
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
FILES_TO_LINT=()
for ((index = 0; index < ${#CANONICAL_FILES[@]}; ++index)); do
  cache_key="${CACHE_KEYS[index]}"
  if [[ "${cache_key}" != "-" && -f "${CACHE_DIR}/${cache_key}" ]]; then
    echo "PASS clang-tidy ${CANONICAL_FILES[index]#"${PROJECT_ROOT}/"} (cached)"
  else
    FILES_TO_LINT+=("${CANONICAL_FILES[index]}")
  fi
done

for ((batch_start = 0; batch_start < ${#FILES_TO_LINT[@]}; batch_start += 2)); do
  batch=("${FILES_TO_LINT[@]:batch_start:2}")
  RunScopedBatch "${batch[@]}" || lint_status=$?
done
exit "${lint_status}"
