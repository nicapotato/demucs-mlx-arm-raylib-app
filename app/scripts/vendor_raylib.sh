#!/usr/bin/env bash
# Vendor raylib into external/raylib-master (gitignored). Pin matches CMake RAYLIB_VERSION_REQUIRED.
set -euo pipefail

RAYLIB_VERSION="${RAYLIB_VERSION:-6.0}"
RAYLIB_REPO="${RAYLIB_REPO:-https://github.com/raysan5/raylib.git}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RAYLIB_DIR="${ROOT}/external/raylib-master"
HEADER="${RAYLIB_DIR}/src/raylib.h"

raylib_version_ok() {
  if [ ! -f "${HEADER}" ]; then
    return 1
  fi
  grep -q '#define RAYLIB_VERSION_MAJOR 6' "${HEADER}" &&
    grep -q '#define RAYLIB_VERSION_MINOR 0' "${HEADER}"
}

if raylib_version_ok; then
  echo "raylib ${RAYLIB_VERSION} already vendored at external/raylib-master"
  exit 0
fi

if [ -d "${RAYLIB_DIR}" ]; then
  echo "Replacing stale raylib at external/raylib-master (need ${RAYLIB_VERSION})"
  rm -rf "${RAYLIB_DIR}"
fi

mkdir -p "${ROOT}/external"
echo "Cloning raylib ${RAYLIB_VERSION} into external/raylib-master"
git clone --depth 1 --branch "${RAYLIB_VERSION}" "${RAYLIB_REPO}" "${RAYLIB_DIR}"

if ! raylib_version_ok; then
  echo "ERROR: vendored raylib is not ${RAYLIB_VERSION} (check ${HEADER})" >&2
  exit 1
fi

echo "raylib ${RAYLIB_VERSION} ready"
