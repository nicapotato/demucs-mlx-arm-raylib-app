"""PyTorch Demucs CLI with the same flags / JSON progress as demucs_mlx.separate."""
from __future__ import annotations

import argparse
import json
import os
import sys
import typing as tp
from pathlib import Path

TORCH_MODELS = (
    "htdemucs",
    "htdemucs_ft",
    "htdemucs_6s",
    "hdemucs_mmi",
    "mdx",
    "mdx_extra",
)

_PROGRESS_JSON = False


def _emit(event: str, **fields: tp.Any) -> None:
    if not _PROGRESS_JSON:
        return
    payload = {"event": event, **fields}
    sys.stdout.write(json.dumps(payload, ensure_ascii=True) + "\n")
    sys.stdout.flush()


def _apply_torch_home(cache_dir: tp.Optional[str]) -> None:
    """Point torch hub downloads at a bundled / explicit cache (offline itch builds)."""
    if not cache_dir:
        cache_dir = os.environ.get("DEMUCS_TORCH_CACHE") or os.environ.get("DEMUCS_MLX_CACHE")
    if not cache_dir:
        return
    root = Path(cache_dir)
    hub = root / "hub"
    torch_home = root if hub.is_dir() else root / "torch"
    torch_home.mkdir(parents=True, exist_ok=True)
    os.environ["TORCH_HOME"] = str(torch_home)


def _resolve_device() -> str:
    forced = os.environ.get("DEMUCS_DEVICE", "").strip().lower()
    if forced in ("cuda", "cpu"):
        if forced == "cuda":
            import torch

            if not torch.cuda.is_available():
                raise SystemExit("DEMUCS_DEVICE=cuda but torch.cuda.is_available() is False")
        return forced
    import torch

    return "cuda" if torch.cuda.is_available() else "cpu"


def _list_models() -> int:
    for name in TORCH_MODELS:
        print(name)
    return 0


def _stem_sources(separator) -> list[str]:
    model = getattr(separator, "model", None)
    sources = getattr(model, "sources", None)
    if sources:
        return [str(s) for s in sources]
    return ["drums", "bass", "other", "vocals"]


def _save_stem(wav, path: Path, samplerate: int, fmt: str, mp3_bitrate: int) -> None:
    from demucs.api import save_audio

    path.parent.mkdir(parents=True, exist_ok=True)
    kwargs: dict[str, tp.Any] = {"samplerate": int(samplerate), "clip": "rescale"}
    if fmt == "mp3":
        kwargs["bitrate"] = int(mp3_bitrate)
    save_audio(wav, str(path), **kwargs)


def _separate_one(
    separator,
    path: Path,
    out_dir: Path,
    fmt: str,
    mp3_bitrate: int,
    track_name: tp.Optional[str],
    verbose: bool,
) -> Path:
    stem_name = track_name if track_name else path.stem
    track_out = out_dir / stem_name
    track_out.mkdir(parents=True, exist_ok=True)
    ext = "mp3" if fmt == "mp3" else "wav"

    _emit("loading", track=str(path), out=str(track_out))
    if verbose and not _PROGRESS_JSON:
        print(f"Loading audio: {path}")

    _emit("separating", track=str(path), done=0, total=0, pct=0.0)
    _origin, stems = separator.separate_audio_file(path)

    samplerate = int(getattr(separator, "samplerate", 44100))
    for name, wav in stems.items():
        stem_path = track_out / f"{name}.{ext}"
        _emit("writing", stem=str(name), path=str(stem_path))
        if verbose and not _PROGRESS_JSON:
            print(f"Wrote: {stem_path}")
        _save_stem(wav, stem_path, samplerate, fmt, mp3_bitrate)
    return track_out


def main(argv: tp.Optional[tp.Sequence[str]] = None) -> int:
    global _PROGRESS_JSON

    parser = argparse.ArgumentParser(
        prog="demucs-torch",
        description="Official Demucs + PyTorch stem separation (Windows CUDA / CPU)",
    )
    parser.add_argument("tracks", nargs="*", help="Audio files to separate")
    parser.add_argument("-n", "--name", default="htdemucs_6s", help="Model name")
    parser.add_argument("-o", "--out", default="separated", help="Output directory")
    parser.add_argument(
        "--track-name",
        default=None,
        help="Override output subfolder name (default: input stem)",
    )
    parser.add_argument("--segment", type=float, default=None, help="Segment length in seconds")
    parser.add_argument("--overlap", type=float, default=0.25, help="Overlap ratio")
    parser.add_argument("--shifts", type=int, default=1, help="Number of random shifts")
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="Accepted for CLI parity with demucs_mlx; official Demucs ignores it",
    )
    parser.add_argument(
        "-b",
        "--batch-size",
        type=int,
        default=8,
        help="Accepted for CLI parity; official Separator does not expose batch size",
    )
    parser.add_argument(
        "--write-workers",
        type=int,
        default=1,
        help="Accepted for CLI parity; stems are written sequentially",
    )
    parser.add_argument(
        "--prefetch-tracks",
        type=int,
        default=0,
        help="Accepted for CLI parity; ignored (Separator loads one file)",
    )
    parser.add_argument("--no-split", action="store_true", help="Disable chunked inference")
    parser.add_argument("--list-models", action="store_true", help="List available models")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose logging")
    parser.add_argument(
        "--mp3",
        action="store_true",
        help="Write MP3 stems instead of WAV",
    )
    parser.add_argument("--mp3-bitrate", type=int, default=128, help="MP3 bitrate kbps")
    parser.add_argument(
        "--progress-json",
        action="store_true",
        help="Emit line-delimited JSON progress events on stdout",
    )
    parser.add_argument(
        "--repo",
        default=None,
        help="Optional local Demucs model repo directory (Separator repo=)",
    )

    args = parser.parse_args(argv)
    _PROGRESS_JSON = bool(args.progress_json)
    _apply_torch_home(None)

    if args.list_models:
        return _list_models()

    if not args.tracks:
        parser.print_help(sys.stderr)
        return 2
    if args.shifts < 0:
        raise SystemExit("--shifts must be >= 0")
    if not (0.0 <= float(args.overlap) < 1.0):
        raise SystemExit("--overlap must be in [0, 1)")
    if args.segment is not None and float(args.segment) <= 0:
        raise SystemExit("--segment must be > 0")
    if args.batch_size <= 0:
        raise SystemExit("--batch-size must be > 0")
    if args.write_workers <= 0:
        raise SystemExit("--write-workers must be > 0")
    if args.prefetch_tracks < 0:
        raise SystemExit("--prefetch-tracks must be >= 0")
    if args.mp3_bitrate <= 0:
        raise SystemExit("--mp3-bitrate must be > 0")
    if args.track_name is not None and len(args.tracks) != 1:
        raise SystemExit("--track-name requires exactly one input track")
    if args.name not in TORCH_MODELS:
        known = ", ".join(TORCH_MODELS)
        raise SystemExit(f"Unknown model '{args.name}'. Available: {known}")

    fmt = "mp3" if args.mp3 else "wav"
    segment = int(args.segment) if args.segment is not None else None
    repo = Path(args.repo) if args.repo else None

    try:
        device = _resolve_device()
        _emit("status", stage="loading_model", model=args.name, device=device)
        if verbose := bool(args.verbose and not _PROGRESS_JSON):
            print(f"Loading Demucs model: {args.name} on {device}")

        def _on_progress(info: dict) -> None:
            audio_length = info.get("audio_length") or 0
            offset = info.get("segment_offset") or 0
            if audio_length:
                pct = 100.0 * float(offset) / float(audio_length)
            else:
                pct = 0.0
            _emit(
                "separating",
                done=int(offset),
                total=int(audio_length),
                pct=round(pct, 2),
                device=device,
            )

        from demucs.api import Separator

        separator = Separator(
            model=args.name,
            repo=repo,
            device=device,
            shifts=int(args.shifts),
            overlap=float(args.overlap),
            split=not args.no_split,
            segment=segment,
            progress=verbose,
            callback=_on_progress if _PROGRESS_JSON else None,
        )
        sources = _stem_sources(separator)
        _emit("status", stage="model_ready", model=args.name, device=device, sources=sources)

        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        for track in args.tracks:
            path = Path(track)
            track_out = _separate_one(
                separator,
                path,
                out_dir,
                fmt=fmt,
                mp3_bitrate=int(args.mp3_bitrate),
                track_name=args.track_name,
                verbose=bool(args.verbose),
            )
            _emit(
                "done",
                track=str(path),
                out=str(track_out),
                sources=sources,
                format=fmt,
                device=device,
            )
    except BaseException as exc:
        _emit("error", message=str(exc))
        if _PROGRESS_JSON:
            return 1
        raise

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
