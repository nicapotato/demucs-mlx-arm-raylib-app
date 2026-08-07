# demucs mlx app

Mac Apple Silicon stem separator GUI. Wraps [demucs-mlx](https://github.com/ssmall256/demucs-mlx) with a raylib front-end, PSARC extract via `rocksmith-psarc`, and rocknroller-compatible 6-stem MP3 output.

Version is owned by [`project.conf`](../project.conf) (`VERSION=x.y.z`).

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

# Externals: raylib (cloned) + checked-in prebuilt rocksmith-psarc .a
make -C app externals

# Convert + cache weights (once)
make -C app models

# Build + run GUI
make -C app run
make -C app version   # prints demucs mlx app x.y.z

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
| `ROCKNROLLER_CHECKOUT` | Only for refreshing prebuilt libs via `app/scripts/refresh_rocksmith_prebuilt.sh` |

## Standalone bundle (local)

```bash
make -C app bundle
# → app/dist/demucs mlx app.app
# → app/dist/demucs-mlx-app-mac-arm64.zip
```

Gatekeeper: unsigned / ad-hoc signed. Users may need right-click -> Open the first time.

## Distribution (itch + GitHub Release)

Large binaries are **not** published to S3. Releases go to:

1. **itch.io** — https://nicapotato.itch.io/demucs-mlx-app (`macos-arm64` channel via butler) — from **ci** or **release**
2. **GitHub Release assets** — zip + sha256 on a `v*` tag — **release** workflow only

Both [ci.yml](../.github/workflows/ci.yml) and [release.yml](../.github/workflows/release.yml) are **workflow_dispatch only** (no push/PR/tag triggers).

### One-time repo secrets

| Secret | Purpose |
|--------|---------|
| `BUTLER_API_KEY` | itch.io API key |

PSARC support uses checked-in arm64 static libs under [`app/external/rocksmith-psarc-prebuilt/`](external/rocksmith-psarc-prebuilt/). Refresh after rocksmith-psarc changes:

```bash
bash app/scripts/refresh_rocksmith_prebuilt.sh
```

### Run CI (tests + itch, no git tag)

From repo root (branch must be pushed). Builds the macOS zip, runs headless smoke, pushes to itch.io. Does **not** create a git tag or GitHub Release.

```bash
make ci                   # or: make ci-watch
make ci PUBLISH_ITCH=false
make ci VERSION=0.1.1
```

Or Actions → **ci** → Run workflow.

### Ship a release (itch + GitHub tag)

1. Bump `VERSION=` in [`project.conf`](../project.conf)
2. Commit + push to `main`
3. From repo root:

```bash
make release              # or: make release-watch
make release VERSION=0.1.1
make release PUBLISH_ITCH=true PUBLISH_GH_RELEASE=false
```

Or Actions → **release** → Run workflow.

## Rocknroller layout

PSARC input `Foo_p.psarc` writes:

```
{output_dir}/Foo_p/drums.mp3
{output_dir}/Foo_p/bass.mp3
...
{output_dir}/Foo_p/piano.mp3
```

Point rocknroller's stems root at the same output folder.
