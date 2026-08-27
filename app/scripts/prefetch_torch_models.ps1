# Download official Demucs checkpoints into app/models/torch (TORCH_HOME).
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Repo = Split-Path -Parent $Root
if ($env:DEMUCS_TORCH_PYTHON) {
    $Python = $env:DEMUCS_TORCH_PYTHON
} else {
    $Python = Join-Path $Repo ".venv\Scripts\python.exe"
}
$Out = Join-Path $Root "models"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$env:DEMUCS_TORCH_CACHE = $Out
$env:PYTHONPATH = $Repo
& $Python -m demucs_torch.prefetch -o $Out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$hfHub = Join-Path $Out "hf\hub"
$torchHub = Join-Path $Out "torch\hub"
if (-not (Test-Path $hfHub) -and -not (Test-Path $torchHub)) {
    Write-Error "Prefetch did not create $hfHub or $torchHub"
}
Write-Host "Prefetched Demucs weights under $Out"
