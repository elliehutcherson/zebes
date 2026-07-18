#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="dev"
BUILD=true

while [[ $# -gt 0 ]]; do
  case $1 in
    --release)
      PRESET="release"
      shift
      ;;
    --no-build)
      BUILD=false
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [--release] [--no-build]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

BUILD_DIR="${PROJECT_ROOT}/build/${PRESET}"
EDITOR="${BUILD_DIR}/bin/zebes_editor"

if [[ "${BUILD}" = true ]]; then
  cmake --preset "${PRESET}" -S "${PROJECT_ROOT}"
  cmake --build --preset "${PRESET}" --target zebes_editor
elif [[ ! -x "${EDITOR}" ]]; then
  echo "Editor executable not found: ${EDITOR}" >&2
  echo "Run without --no-build to build it first." >&2
  exit 1
fi

cd "${BUILD_DIR}/bin"
exec "${EDITOR}"
