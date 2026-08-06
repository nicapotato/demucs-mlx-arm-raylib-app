# demucs mlx app

Mac Apple Silicon stem separator GUI. Wraps [demucs-mlx](https://github.com/ssmall256/demucs-mlx) with a raylib front-end, PSARC extract via `rocksmith-psarc`, and rocknroller-compatible 6-stem MP3 output.

## Features

- Drag-drop / file picker: MP3, WAV, OGG, FLAC, M4A, **PSARC**
- Default model: `htdemucs_6s` (drums, bass, other, vocals, guitar, piano)
- Output: MP3 (default) or WAV to a user-selected folder
- Background worker subprocess (GUI stays responsive)
- Standalone `.app` bundle with frozen MLX worker + bundled weights

## Dev (on this machine)

```bash
# From repo root
uv venv .venv --python 3.12
source .venv/bin/activate
uv pip install -e '.[convert]' lameenc pyinstaller

# Convert + cache weights (once)
make -C app models

# Build + run GUI
make -C app run

# Acceptance checks
make -C app verify-mp3
make -C app verify-psarc
```

Env overrides:

| Variable | Purpose |
|----------|---------|
| `DEMUCS_MLX_PYTHON` | Dev python that has `demucs_mlx` |
| `DEMUCS_MLX_WORKER` | Path to frozen worker binary |
| `DEMUCS_MLX_CACHE` | Directory with `htdemucs_6s_mlx.pkl` |

## Standalone bundle (itch-ready zip)

```bash
make -C app bundle
# → app/dist/demucs mlx app.app
# → app/dist/demucs-mlx-app-mac-arm64.zip
```

The zip is self-contained (GUI + frozen worker + MLX weights). No Python install required on the target Mac.

Gatekeeper: unsigned / ad-hoc signed. Users may need right-click -> Open the first time.

## Rocknroller layout

PSARC input `Foo_p.psarc` writes:

```
{output_dir}/Foo_p/drums.mp3
{output_dir}/Foo_p/bass.mp3
...
{output_dir}/Foo_p/piano.mp3
```

Point rocknroller's stems root at the same output folder.
