#!/usr/bin/env bash
# Rebuild librocksmith_psarc.a + libvgmstream.a from a rocknroller checkout and
# copy them (plus public headers) into app/external/rocksmith-psarc-prebuilt/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "${ROOT}/.." && pwd)"
PRE="${ROOT}/external/rocksmith-psarc-prebuilt"
RS="${ROCKNROLLER_CHECKOUT:-${REPO}/../rocknroller}/rocksmith-psarc"

if [[ ! -f "${RS}/CMakeLists.txt" ]]; then
  echo "ERROR: rocksmith-psarc source not found at ${RS}" >&2
  echo "Set ROCKNROLLER_CHECKOUT to your rocknroller repo root." >&2
  exit 1
fi

BUILD="${RS}/build-prebuilt-arm64"
echo "Configuring ${RS} -> ${BUILD}"
cmake -S "${RS}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD}" -j --target rocksmith_psarc

LIB_RS="${BUILD}/librocksmith_psarc.a"
LIB_VGM="${RS}/.vgmstream-cmake-build/src/libvgmstream.a"
if [[ ! -f "${LIB_VGM}" ]]; then
  # Fallback: search under rocksmith-psarc for the native (non-wasm) lib
  LIB_VGM="$(find "${RS}" -path '*wasm*' -prune -o -name 'libvgmstream.a' -print | head -1 || true)"
fi
if [[ ! -f "${LIB_RS}" || ! -f "${LIB_VGM}" ]]; then
  echo "ERROR: missing libs: RS=${LIB_RS} VGM=${LIB_VGM}" >&2
  exit 1
fi

mkdir -p "${PRE}/include" "${PRE}/lib"
cp -f "${RS}/include/rocksmith_psarc.h" "${RS}/include/rs_sng_mask.h" "${PRE}/include/"
cp -f "${LIB_RS}" "${PRE}/lib/librocksmith_psarc.a"
cp -f "${LIB_VGM}" "${PRE}/lib/libvgmstream.a"

echo "Updated prebuilt at ${PRE}"
file "${PRE}/lib/"*.a
du -sh "${PRE}/lib/"*.a
