#!/usr/bin/env bash
# Clone public vgmstream into rocksmith-psarc-src/external/vgmstream (gitignored).
set -euo pipefail

VGM_REPO="${VGM_REPO:-https://github.com/vgmstream/vgmstream.git}"
# Pin matches the rocknroller checkout used for macOS prebuilts.
VGM_COMMIT="${VGM_COMMIT:-60dca6602c1540f8041092396595e7e8f048a612}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VGM_DIR="${ROOT}/external/rocksmith-psarc-src/external/vgmstream"

if [[ -f "${VGM_DIR}/CMakeLists.txt" ]]; then
  if [[ -d "${VGM_DIR}/.git" ]]; then
    have="$(git -C "${VGM_DIR}" rev-parse HEAD 2>/dev/null || true)"
    if [[ "$have" == "$VGM_COMMIT" ]]; then
      echo "vgmstream ${VGM_COMMIT} already vendored"
      exit 0
    fi
  else
    echo "vgmstream present at ${VGM_DIR}"
    exit 0
  fi
  echo "Replacing vgmstream at ${VGM_DIR} (need ${VGM_COMMIT})"
  rm -rf "${VGM_DIR}"
fi

mkdir -p "$(dirname "${VGM_DIR}")"
echo "Cloning vgmstream ${VGM_COMMIT}"
git init "${VGM_DIR}"
git -C "${VGM_DIR}" remote add origin "${VGM_REPO}"
git -C "${VGM_DIR}" fetch --depth 1 origin "${VGM_COMMIT}"
git -C "${VGM_DIR}" checkout --detach FETCH_HEAD
test -f "${VGM_DIR}/CMakeLists.txt"
echo "vgmstream ${VGM_COMMIT} ready"
