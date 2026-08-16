"""Download official Demucs checkpoints into TORCH_HOME for offline Windows bundles."""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from .separate import TORCH_MODELS, _apply_torch_home


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Prefetch official Demucs weights into TORCH_HOME")
    parser.add_argument(
        "-o",
        "--out",
        default=None,
        help="Cache directory (default: DEMUCS_TORCH_CACHE / app/models)",
    )
    parser.add_argument(
        "-n",
        "--name",
        action="append",
        dest="names",
        help="Model name (repeatable). Default: htdemucs_6s and htdemucs",
    )
    args = parser.parse_args(argv)

    out = args.out
    if not out:
        out = os.environ.get("DEMUCS_TORCH_CACHE") or os.environ.get("DEMUCS_MLX_CACHE")
    if not out:
        here = Path(__file__).resolve().parents[1]
        out = str(here / "app" / "models")
    _apply_torch_home(out)

    names = args.names or ["htdemucs_6s", "htdemucs"]
    for name in names:
        if name not in TORCH_MODELS:
            print(f"unknown model: {name}", file=sys.stderr)
            return 2
        print(f"prefetch {name} -> TORCH_HOME={os.environ.get('TORCH_HOME')}")
        from demucs.pretrained import get_model

        model = get_model(name)
        del model
        print(f"ok {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
