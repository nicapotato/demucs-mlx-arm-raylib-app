# Platform notes

This project is uv-first on macOS. Windows uses a separate official-Demucs + PyTorch worker.

## macOS (Apple Silicon)

- Apple Silicon is supported via MLX.
- Audio I/O is handled natively by mlx-audio-io (no FFmpeg required).

Typical flow:

```bash
uv lock
uv sync
uv run demucs-mlx /path/to/audio.wav
```

GUI env vars:

| Variable | Purpose |
|----------|---------|
| `DEMUCS_MLX_PYTHON` | Dev python that has `demucs_mlx` |
| `DEMUCS_MLX_WORKER` | Path to frozen MLX worker binary |
| `DEMUCS_MLX_CACHE` | Directory with `htdemucs_6s_mlx.pkl` |

## Windows (x86_64)

Official Meta Demucs + PyTorch. NVIDIA CUDA is the fast path; CPU works but is slow
(a 4-minute track may take about a minute). AMD/Intel GPUs fall back to CPU.

Users need a recent NVIDIA driver, not a full CUDA toolkit. The frozen worker ships
the CUDA runtime.

```powershell
python -m pip install torch==2.2.2 torchaudio==2.2.2 --index-url https://download.pytorch.org/whl/cu121
python -m pip install -r requirements-windows.txt
$env:PYTHONPATH = (Get-Location)
$env:DEMUCS_DEVICE = "cuda"   # or cpu
python -m demucs_torch.separate song.mp3 -n htdemucs_6s -o stems --mp3 --track-name song
```

GUI / CI env vars:

| Variable | Purpose |
|----------|---------|
| `DEMUCS_TORCH_PYTHON` | Dev python that has `demucs` + torch |
| `DEMUCS_TORCH_WORKER` | Path to frozen `demucs_torch_worker.exe` |
| `DEMUCS_TORCH_CACHE` | Directory used as model cache (`TORCH_HOME` = `cache/torch`) |
| `DEMUCS_DEVICE` | `cuda` or `cpu` (default: cuda if available) |

itch.io channel: `windows-x86-64` (`DemucsMLX-windows-x86_64.zip`).

VRAM: default `htdemucs_6s` segments want about 5–7 GB. 3–4 GB cards may OOM; set
`DEMUCS_DEVICE=cpu` or wait for a later `--segment` control.

Unsigned PyInstaller onedirs trip SmartScreen (“Windows protected your PC”) the same
way unsigned Mac `.app` bundles trip Gatekeeper. Right-click → Open / More info → Run anyway.

Windows PSARC: CI builds `app/external/rocksmith-psarc-src` plus a public vgmstream
clone (`app/scripts/vendor_vgmstream.sh`). The GUI uses `/MT` and vcpkg
`x64-windows-static` so itch users are not asked to install VC++ redistributable.

Optional MSVC `.lib` prebuilts can still be generated with
`pwsh app/scripts/refresh_rocksmith_prebuilt_windows.ps1`.

## Linux

- Python >= 3.10 required.

```bash
uv lock
uv sync
uv run demucs-mlx /path/to/audio.wav
```

The raylib GUI is not packaged for Linux.
