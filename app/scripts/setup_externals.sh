#!/usr/bin/env bash
# Ensure app/external/{raylib-master,tinyfiledialogs,rocksmith-psarc-prebuilt} are ready.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXT="${ROOT}/external"

mkdir -p "${EXT}"

if [[ ! -f "${EXT}/tinyfiledialogs/tinyfiledialogs.c" ]]; then
  echo "ERROR: missing ${EXT}/tinyfiledialogs/tinyfiledialogs.c (should be git-tracked)" >&2
  exit 1
fi

if [[ ! -f "${EXT}/rocksmith-psarc-prebuilt/lib/librocksmith_psarc.a" ||
      ! -f "${EXT}/rocksmith-psarc-prebuilt/lib/libvgmstream.a" ||
      ! -f "${EXT}/rocksmith-psarc-prebuilt/include/rocksmith_psarc.h" ]]; then
  echo "ERROR: missing prebuilt rocksmith-psarc under ${EXT}/rocksmith-psarc-prebuilt" >&2
  echo "Refresh with: bash app/scripts/refresh_rocksmith_prebuilt.sh" >&2
  exit 1
fi

bash "${ROOT}/scripts/vendor_raylib.sh"

test -f "${EXT}/raylib-master/CMakeLists.txt"
echo "externals ready (raylib + prebuilt rocksmith-psarc)"
