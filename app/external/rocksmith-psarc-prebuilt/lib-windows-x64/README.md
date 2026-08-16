# Windows x64 rocksmith-psarc prebuilts

MSVC `.lib` files used by the Windows DemucsMLX GUI so CI does not need the private
`rocknroller` checkout.

| File | Role |
|------|------|
| `rocksmith_psarc.lib` | PSARC + WEM glue |
| `vgmstream.lib` | Wwise decode (vendored vgmstream) |

Generate on a Windows machine (VS + vcpkg), then commit the two `.lib` files:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
$env:ROCKNROLLER_CHECKOUT = "C:\path\to\rocknroller"
pwsh app/scripts/refresh_rocksmith_prebuilt_windows.ps1
```

Until these files are committed, Windows CMake builds
`app/external/rocksmith-psarc-src` after `bash app/scripts/vendor_vgmstream.sh`.
