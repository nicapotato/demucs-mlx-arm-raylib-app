# Assemble DemucsMLX Windows zip: GUI + frozen torch worker + models + resources + CRT.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Repo = Split-Path -Parent $Root
$ExeCandidates = @(
    (Join-Path $Root "build\Release\DemucsMLX.exe"),
    (Join-Path $Root "build\DemucsMLX.exe")
)
$Exe = $ExeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$WorkerDir = Join-Path $Root "dist\worker\demucs_torch_worker"
$Models = Join-Path $Root "models"
$Branding = Join-Path $Repo "resources"
$Release = Join-Path $Root "dist\DemucsMLX"
$ZipName = "DemucsMLX-windows-x86_64.zip"
$ZipPath = Join-Path $Root "dist\$ZipName"

$Version = "0.0.0"
$Conf = Join-Path $Repo "project.conf"
if (Test-Path $Conf) {
    $line = Select-String -Path $Conf -Pattern "^VERSION=" | Select-Object -First 1
    if ($line) {
        $Version = ($line.Line -split "=", 2)[1].Trim()
        if (-not $Version) { $Version = "0.0.0" }
    }
}
Write-Host "Packaging DemucsMLX v$Version (Windows x86_64)"

if (-not $Exe) {
    Write-Error "Build the GUI first (CMake Release). Missing DemucsMLX.exe"
}
if (-not (Test-Path (Join-Path $WorkerDir "demucs_torch_worker.exe"))) {
    Write-Error "Freeze the worker first: pwsh app/scripts/freeze_worker_torch.ps1"
}
if (-not (Test-Path $Branding)) {
    Write-Error "Missing branding resources at $Branding"
}

if (Test-Path $Release) { Remove-Item -Recurse -Force $Release }
New-Item -ItemType Directory -Force -Path $Release | Out-Null
Copy-Item -Force $Exe (Join-Path $Release "DemucsMLX.exe")
Copy-Item -Recurse -Force $WorkerDir (Join-Path $Release "worker")
New-Item -ItemType Directory -Force -Path (Join-Path $Release "models") | Out-Null
if (Test-Path $Models) {
    Copy-Item -Recurse -Force "$Models\*" (Join-Path $Release "models")
}
Copy-Item -Recurse -Force $Branding (Join-Path $Release "resources")

if ($env:VCPKG_ROOT) {
    $vcpkgBin = Join-Path $env:VCPKG_ROOT "installed\x64-windows\bin"
    if (Test-Path $vcpkgBin) {
        Get-ChildItem "$vcpkgBin\*.dll" -ErrorAction SilentlyContinue | Copy-Item -Destination $Release -Force
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsInstall = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $crtDir = $null
    if ($vsInstall) {
        $redistRoot = Join-Path $vsInstall "VC\Redist\MSVC"
        if (Test-Path $redistRoot) {
            foreach ($ver in Get-ChildItem $redistRoot -Directory | Sort-Object Name -Descending) {
                $x64Dir = Join-Path $ver.FullName "x64"
                if (-not (Test-Path $x64Dir)) { continue }
                $candidate = Get-ChildItem $x64Dir -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
                    Where-Object { Test-Path (Join-Path $_.FullName "msvcp140.dll") } |
                    Select-Object -First 1
                if ($candidate) {
                    $crtDir = $candidate
                    break
                }
            }
        }
    }
    if (-not $crtDir) {
        $crtDll = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find "VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\msvcp140.dll" | Select-Object -First 1
        if ($crtDll) { $crtDir = Get-Item (Split-Path $crtDll) }
    }
    if ($crtDir) {
        Get-ChildItem "$($crtDir.FullName)\*.dll" | Copy-Item -Destination $Release -Force
        Write-Host "Bundled MSVC CRT from $($crtDir.FullName)"
    } else {
        Write-Warning "Could not locate MSVC CRT; zip may fail on machines without VC++ redistributable"
    }
}

if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path "$Release\*" -DestinationPath $ZipPath -Force
$bytes = (Get-Item $ZipPath).Length
Write-Host "Created $ZipPath ($bytes bytes)"
$under2g = $bytes -lt 2147483648
Write-Host "GH_RELEASE_OK=$under2g"
if ($env:GITHUB_OUTPUT) {
    "zip_bytes=$bytes" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "gh_release_ok=$under2g" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}
