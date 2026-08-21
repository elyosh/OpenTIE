# Application icons

All platform icon assets derive from one committed master image. The artwork
must be original; extracting icon resources from the original game executable
would redistribute copyrighted assets.

## Files

| File | Role |
|-|-|
| `packaging/icons/opentie-icon.png` | Master image (input) |
| `packaging/icons/icon_tools.py` | Regenerates every derived asset |
| `cmake/windows/tie.ico` | Windows executable icon (generated, committed) |
| `cmake/macos/TIE.icns` | macOS bundle icon (generated, committed) |
| `src/tie_app/window_icon.h` | Embedded window/taskbar icon (generated, committed) |

The derived assets are committed so builds do not require image tooling. The
generated files live under `cmake/` and `src/` because those directories are
part of the Docker build context, while `packaging/` is not.

## Regenerating

```sh
python3 packaging/icons/icon_tools.py
```

The generator uses only the Python standard library and runs on any platform.
Commit the three regenerated outputs together with the new master image.

## Platform usage

- **Windows**: `cmake/windows/tie.rc.in` embeds `tie.ico` into `OpenTIE.exe`.
- **macOS**: `TIE.icns` is copied into the application bundle and referenced by
  `CFBundleIconFile` in `cmake/macos/Info.plist.in`.
- **Window/taskbar icon**: `src/tie_app/application.c` passes the embedded BMP
  to Aeron. Aeron applies it on Windows and Linux, but leaves the macOS bundle
  icon authoritative.
- **Linux desktop**: ELF binaries do not contain an application icon. The
  runtime icon covers the application window and taskbar; installed launchers
  can reference the master PNG separately.

## Master artwork

The master must be a 1024x1024, 8-bit RGB or RGBA, non-interlaced PNG. Use a
transparent background outside the icon shape and ensure the design remains
recognizable when downscaled to 16x16 and 32x32.
