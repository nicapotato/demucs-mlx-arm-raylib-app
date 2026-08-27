# Rebuild rocksmith_psarc.lib + vgmstream.lib from a rocknroller checkout and
# copy them into app/external/rocksmith-psarc-prebuilt/lib-windows-x64/.
# Run on Windows with VS + vcpkg (same toolchain as rocknroller Windows CI).
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Repo = Split-Path -Parent $Root
$Pre = Join-Path $Root "external\rocksmith-psarc-prebuilt"
$Dest = Join-Path $Pre "lib-windows-x64"

if ($env:ROCKNROLLER_CHECKOUT) {
    $Rs = Join-Path $env:ROCKNROLLER_CHECKOUT "rocksmith-psarc"
} else {
    $Rs = Join-Path (Split-Path -Parent $Repo) "rocknroller\rocksmith-psarc"
}

if (-not (Test-Path (Join-Path $Rs "CMakeLists.txt"))) {
    Write-Error "rocksmith-psarc source not found at $Rs. Set ROCKNROLLER_CHECKOUT to your rocknroller repo root."
}

if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Install vcpkg and: vcpkg install zlib:x64-windows-static libogg:x64-windows-static libvorbis:x64-windows-static"
}

$Toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
$Build = Join-Path $Rs "build-prebuilt-win64"
Write-Host "Configuring $Rs -> $Build"
cmake -S $Rs -B $Build -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=$Toolchain `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
    -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $Build --config Release --target rocksmith_psarc --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$LibRs = @(
    (Join-Path $Build "Release\rocksmith_psarc.lib"),
    (Join-Path $Build "rocksmith_psarc.lib")
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$LibVgm = Get-ChildItem -Path $Rs -Recurse -Filter "libvgmstream.lib" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch "wasm" } |
    Select-Object -First 1
if (-not $LibVgm) {
    $LibVgm = Get-ChildItem -Path $Rs -Recurse -Filter "vgmstream.lib" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "wasm" } |
        Select-Object -First 1
}

if (-not $LibRs -or -not $LibVgm) {
    Write-Error "missing libs: RS=$LibRs VGM=$($LibVgm.FullName)"
}

New-Item -ItemType Directory -Force -Path (Join-Path $Pre "include") | Out-Null
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item -Force (Join-Path $Rs "include\rocksmith_psarc.h") (Join-Path $Pre "include\")
Copy-Item -Force (Join-Path $Rs "include\rs_sng_mask.h") (Join-Path $Pre "include\")
Copy-Item -Force $LibRs (Join-Path $Dest "rocksmith_psarc.lib")
Copy-Item -Force $LibVgm.FullName (Join-Path $Dest "vgmstream.lib")

Write-Host "Updated prebuilt at $Dest"
Get-ChildItem $Dest | Format-Table Name, Length
