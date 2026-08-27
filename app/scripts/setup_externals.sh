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

if [[ ! -f "${EXT}/rocksmith-psarc-prebuilt/include/rocksmith_psarc.h" ]]; then
  echo "ERROR: missing ${EXT}/rocksmith-psarc-prebuilt/include/rocksmith_psarc.h" >&2
  exit 1
fi
is_windows=0
case "$(uname -s 2>/dev/null || echo unknown)" in
  MINGW*|MSYS*|CYGWIN*) is_windows=1 ;;
esac

if [[ "$is_windows" -eq 1 ]]; then
  if [[ ! -f "${EXT}/rocksmith-psarc-prebuilt/lib-windows-x64/rocksmith_psarc.lib" &&
        ! -f "${EXT}/rocksmith-psarc-src/CMakeLists.txt" &&
        ! -f "${EXT}/rocksmith-psarc/CMakeLists.txt" ]]; then
    echo "ERROR: no Windows rocksmith-psarc prebuilt or source under ${EXT}" >&2
    exit 1
  fi
else
  if [[ ! -f "${EXT}/rocksmith-psarc-prebuilt/lib/librocksmith_psarc.a" ||
        ! -f "${EXT}/rocksmith-psarc-prebuilt/lib/libvgmstream.a" ]]; then
    echo "ERROR: missing prebuilt rocksmith-psarc under ${EXT}/rocksmith-psarc-prebuilt/lib" >&2
    echo "Refresh with: bash app/scripts/refresh_rocksmith_prebuilt.sh" >&2
    exit 1
  fi
fi

bash "${ROOT}/scripts/vendor_raylib.sh"
if [[ "$is_windows" -eq 1 && ! -f "${EXT}/rocksmith-psarc-prebuilt/lib-windows-x64/rocksmith_psarc.lib" ]]; then
  bash "${ROOT}/scripts/vendor_vgmstream.sh"
fi

test -f "${EXT}/raylib-master/CMakeLists.txt"
echo "externals ready (raylib + rocksmith-psarc)"
