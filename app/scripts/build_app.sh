#!/usr/bin/env bash
# Assemble "demucs mlx app.app" for local / itch distribution (standalone).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
BIN="$ROOT/build/demucs_mlx_app"
WORKER_DIR="$ROOT/dist/worker/demucs_mlx_worker"
MODELS="$ROOT/models"
BRANDING="$REPO/resources"
APP_NAME="demucs mlx app"
APP="$ROOT/dist/${APP_NAME}.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
RES="$CONTENTS/Resources"
ICON_SRC="$BRANDING/demucs-mlx-app-initials.png"

VERSION="0.0.0"
if [[ -f "$REPO/project.conf" ]]; then
  VERSION="$(grep '^VERSION=' "$REPO/project.conf" | cut -d= -f2 | tr -d '[:space:]')"
  if [[ -z "$VERSION" ]]; then
    VERSION="0.0.0"
  fi
fi
echo "Packaging demucs mlx app v${VERSION}"

if [[ ! -x "$BIN" ]]; then
  echo "Build the GUI first: make -C app build" >&2
  exit 1
fi
if [[ ! -x "$WORKER_DIR/demucs_mlx_worker" ]]; then
  echo "Freeze the worker first: make -C app worker" >&2
  exit 1
fi
if [[ ! -f "$MODELS/htdemucs_6s_mlx.pkl" ]]; then
  echo "Missing bundled model $MODELS/htdemucs_6s_mlx.pkl - run: make -C app models" >&2
  exit 1
fi
if [[ ! -f "$ICON_SRC" ]]; then
  echo "Missing icon source $ICON_SRC" >&2
  exit 1
fi

rm -rf "$APP"
mkdir -p "$MACOS/resources" "$RES/worker" "$RES/models" "$RES/resources"

cp "$BIN" "$MACOS/demucs-mlx-app"
cp -R "$WORKER_DIR/." "$RES/worker/"
cp -R "$MODELS/." "$RES/models/"
# Branding for SearchAndSetResourceDir / GetApplicationDirectory fallbacks
cp -R "$BRANDING/." "$MACOS/resources/"
cp -R "$BRANDING/." "$RES/resources/"

# Fail loud if any Mach-O in the .app still expects Homebrew / non-system dylibs.
# (Crash on itch users: DYLD "Library not loaded: /opt/homebrew/.../libvorbisfile...")
homebrew_hits="$(
  find "$APP" \( -type f -perm -111 -o -name '*.dylib' -o -name '*.so' \) -print0 \
    | while IFS= read -r -d '' f; do
        if file -b "$f" 2>/dev/null | grep -q 'Mach-O'; then
          hits="$(otool -L "$f" 2>/dev/null | grep -E '/opt/homebrew|/usr/local/opt|/usr/local/Cellar' || true)"
          if [[ -n "$hits" ]]; then
            echo "$f"
            echo "$hits"
          fi
        fi
      done
)"
if [[ -n "$homebrew_hits" ]]; then
  echo "ERROR: bundled app still links Homebrew dylibs:" >&2
  echo "$homebrew_hits" >&2
  echo "GUI must static-link libvorbis/libogg; worker must ship its own @rpath dylibs." >&2
  exit 1
fi

# Build .icns from pixel-art initials with nearest-neighbor upscale (rocknroller-style iconset)
ICONSET="$ROOT/dist/app-icon.iconset"
ICNS_OUT="$RES/app-icon.icns"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"

nn_scale() {
  local size="$1"
  local out="$2"
  local tmp
  tmp="$(mktemp -t dmx_icon).png"
  # Write via a normal .png temp path: ffmpeg rejects '@' in output names.
  ffmpeg -y -hide_banner -loglevel error \
    -i "$ICON_SRC" \
    -vf "scale=${size}:${size}:flags=neighbor" \
    "$tmp"
  mv -f "$tmp" "$out"
}

nn_scale 16  "$ICONSET/icon_16x16.png"
nn_scale 32  "$ICONSET/diana.k@example.org"
nn_scale 32  "$ICONSET/icon_32x32.png"
nn_scale 64  "$ICONSET/ivan.p@example.net"
nn_scale 128 "$ICONSET/icon_128x128.png"
nn_scale 256 "$ICONSET/wendy.h@example.net"
nn_scale 256 "$ICONSET/icon_256x256.png"
nn_scale 512 "$ICONSET/wendy.h@example.net"
nn_scale 512 "$ICONSET/icon_512x512.png"
nn_scale 1024 "$ICONSET/walt.e@example.net"

iconutil -c icns "$ICONSET" -o "$ICNS_OUT"
rm -rf "$ICONSET"

cat >"$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>demucs mlx app</string>
  <key>CFBundleDisplayName</key><string>demucs mlx app</string>
  <key>CFBundleIdentifier</key><string>com.nicapotato.demucs-mlx-app</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>demucs-mlx-app</string>
  <key>CFBundleIconFile</key><string>app-icon</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Warn if GUI minos is above Info.plist claim (build machine OS leaked into binary).
gui_minos="$(otool -l "$MACOS/demucs-mlx-app" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')"
plist_minos="$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$CONTENTS/Info.plist" 2>/dev/null || echo "?")"
echo "GUI minos=${gui_minos:-unknown}  Info.plist LSMinimumSystemVersion=${plist_minos}"
if [[ -n "${gui_minos:-}" && "$plist_minos" != "?" ]]; then
  if awk -v a="$gui_minos" -v b="$plist_minos" 'BEGIN{split(a,A,".");split(b,B,"."); exit !((A[1]>B[1]) || (A[1]==B[1] && A[2]>B[2]))}'; then
    echo "WARNING: GUI binary requires macOS ${gui_minos} but Info.plist claims ${plist_minos}." >&2
    echo "Set CMAKE_OSX_DEPLOYMENT_TARGET=${plist_minos} and rebuild." >&2
  fi
fi

# Ad-hoc sign so Gatekeeper is slightly less angry locally
codesign --force --deep --sign - "$APP" 2>/dev/null || true

# Zip for itch upload
ZIP="$ROOT/dist/demucs-mlx-app-mac-arm64.zip"
rm -f "$ZIP"
(
  cd "$ROOT/dist"
  ditto -c -k --keepParent "${APP_NAME}.app" "demucs-mlx-app-mac-arm64.zip"
)

echo "App: $APP"
echo "Zip: $ZIP"
echo "Icon: $ICNS_OUT"
du -sh "$APP" "$ZIP"
ls -la "$ICNS_OUT"
