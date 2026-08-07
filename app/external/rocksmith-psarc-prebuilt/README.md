# Prebuilt rocksmith-psarc (macOS arm64)

Static libraries + public headers used by demucs mlx app so CI does not need the private
`rocknroller` checkout.

| File | Role |
|------|------|
| `include/*.h` | Public API |
| `lib/librocksmith_psarc.a` | PSARC + WEM glue |
| `lib/libvgmstream.a` | Wwise decode (vendored vgmstream) |

**Build-time deps (static link into the app binary):** zlib (system), libvorbis, libogg (`brew install libvorbis libogg`). End users do not need Homebrew.

Refresh from a sibling rocknroller tree:

```bash
bash app/scripts/refresh_rocksmith_prebuilt.sh
```
