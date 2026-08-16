# Freeze demucs_torch CLI into a standalone onedir worker with PyInstaller.
# Inference uses official Demucs + CUDA torch. Weights live under TORCH_HOME
# (app/models/torch after prefetch).
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Repo = Split-Path -Parent $Root
if ($env:DEMUCS_TORCH_PYTHON) {
    $Python = $env:DEMUCS_TORCH_PYTHON
} else {
    $Python = Join-Path $Repo ".venv\Scripts\python.exe"
}
$Dist = Join-Path $Root "dist\worker"
$SpecWork = Join-Path $Root "dist\pyi-torch"

if (-not (Test-Path $Python)) {
    Write-Error "Missing python at $Python — create a Windows venv first"
}

& $Python -c "import PyInstaller" 2>$null
if ($LASTEXITCODE -ne 0) {
    & $Python -m pip install pyinstaller
}

if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
if (Test-Path $SpecWork) { Remove-Item -Recurse -Force $SpecWork }
New-Item -ItemType Directory -Force -Path $SpecWork | Out-Null

$Entry = Join-Path $SpecWork "demucs_torch_worker.py"
@'
"""Frozen official-Demucs worker entry."""
from demucs_torch.separate import main

raise SystemExit(main())
'@ | Set-Content -Path $Entry -Encoding UTF8

Push-Location $SpecWork
try {
    & $Python -m PyInstaller `
        --noconfirm `
        --clean `
        --onedir `
        --name demucs_torch_worker `
        --paths $Repo `
        --collect-all torch `
        --collect-all torchaudio `
        --collect-all demucs `
        --hidden-import julius `
        --hidden-import lameenc `
        --hidden-import soundfile `
        --hidden-import yaml `
        --hidden-import demucs_torch.separate `
        --exclude-module mlx `
        --exclude-module mlx_audio_io `
        --exclude-module mlx_spectro `
        --exclude-module tensorflow `
        --exclude-module jax `
        --exclude-module torchvision `
        --distpath $Dist `
        --workpath (Join-Path $SpecWork "build") `
        $Entry
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

$Bin = Join-Path $Dist "demucs_torch_worker\demucs_torch_worker.exe"
if (-not (Test-Path $Bin)) {
    Write-Error "Freeze failed: missing $Bin"
}
Write-Host "Worker frozen at $Bin"
Get-ChildItem (Join-Path $Dist "demucs_torch_worker") | Measure-Object -Property Length -Sum | ForEach-Object {
    Write-Host ("Worker onedir bytes: {0}" -f $_.Sum)
}
