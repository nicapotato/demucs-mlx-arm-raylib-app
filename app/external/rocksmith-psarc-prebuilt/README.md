# Prebuilt rocksmith-psarc

Static libraries + public headers used by DemucsMLX so CI does not need the private
`rocknroller` checkout.

| File | Role |
|------|------|
| `include/*.h` | Public API |
| `lib/librocksmith_psarc.a` | macOS arm64 PSARC + WEM glue |
| `lib/libvgmstream.a` | macOS arm64 Wwise decode |
| `lib-windows-x64/rocksmith_psarc.lib` | Windows x64 PSARC + WEM glue |
| `lib-windows-x64/vgmstream.lib` | Windows x64 Wwise decode |

**macOS build-time deps (static link):** zlib (system), libvorbis, libogg (`brew install libvorbis libogg`). End users do not need Homebrew.

**Windows build-time deps:** vcpkg `zlib:x64-windows-static` `libogg:x64-windows-static` `libvorbis:x64-windows-static` (`/MT`).

Refresh from a sibling rocknroller tree:

```bash
bash app/scripts/refresh_rocksmith_prebuilt.sh
```

```powershell
pwsh app/scripts/refresh_rocksmith_prebuilt_windows.ps1
```
