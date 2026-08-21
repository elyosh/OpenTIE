# macOS package build

This native build produces a relocatable `OpenTIE.app` and a DMG disk
image for the current Mac architecture. It downloads and builds pinned SDL3,
zstd, FFmpeg, FluidSynth, SpeexDSP, and SDL_shadercross sources into a private
build directory. Homebrew libraries are not embedded in the application.

The default package targets macOS 13 or later. The generated application
contains only the FFmpeg decoder and demuxer needed by OpenTIE.

## Requirements

- macOS with Xcode or the Xcode command-line tools
- CMake 3.23 or later
- Ninja
- pkg-config
- Git and Make
- Autoconf, Automake, Libtool, and GLib

The non-Apple build tools can be installed with Homebrew:

```sh
brew install autoconf automake cmake glib libtool ninja pkg-config
```

## Build

Run from the repository root:

```sh
./packaging/macos/build-package.sh
```

On Apple silicon the output is:

```text
build/artifacts/opentie-0.0.0-dev-macos-arm64-release.dmg
build/cache/macos-arm64/stage-release/OpenTIE.app
```

`build/artifacts` holds final packages; `build/cache/macos-<architecture>`
holds the dependency sources, install prefixes, and build trees. The cache is
safe to delete but expensive to rebuild because it includes the vendored DXC
compiler.

The DMG volume contains `OpenTIE.app` and an `/Applications` symlink for
drag-and-drop installation. It is an APFS image with LZMA compression, which
requires macOS 10.15 or later to open.

The script performs an ad-hoc signature when no signing identity is supplied
and leaves the DMG itself unsigned. This is suitable for local testing but not
for public distribution through Gatekeeper.

## Build variants

`TIE_BUILD_TYPE` defaults to `Release` and accepts only `Release` or `Debug`:

```sh
TIE_BUILD_TYPE=Debug ./packaging/macos/build-package.sh
```

The Debug output is
`build/artifacts/opentie-<version>-macos-arm64-debug.dmg`. Debug packages enable
the in-game debug UI and include an `OpenTIE.dSYM` debug-symbol bundle
beside the application in the DMG. Dependencies are always built in Release
mode; only OpenTIE changes configuration, so both variants share the
cached dependency builds.

The script intentionally builds only the native architecture. Build the arm64
package on an Apple silicon Mac and the x86-64 package on an Intel Mac. This
keeps every host shader tool and embedded library on the same architecture.

## Deployment target and package metadata

The defaults can be overridden through environment variables:

```sh
TIE_MACOS_DEPLOYMENT_TARGET=13.0 \
TIE_VERSION=1.2.3 \
TIE_MACOS_BUILD_VERSION=1 \
TIE_MACOS_BUNDLE_IDENTIFIER=org.totallyopen.opentie \
./packaging/macos/build-package.sh
```

`TIE_VERSION` defaults to `0.0.0-dev` and appears in both the package name and
the application bundle version. CI passes the short commit hash for
development builds and the tag version for releases.

The same deployment target is applied to OpenTIE and every bundled
dependency. Compatibility must be checked on a clean installation of the
oldest supported macOS version.

## Developer ID signing and notarization

Store notarization credentials once:

```sh
xcrun notarytool store-credentials TIE_NOTARY \
  --apple-id "developer@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password"
```

Then build, sign, and notarize:

```sh
TIE_MACOS_SIGN_IDENTITY="Developer ID Application: Name (TEAMID)" \
TIE_MACOS_NOTARY_PROFILE=TIE_NOTARY \
./packaging/macos/build-package.sh
```

For non-interactive environments such as CI, pass the credentials directly:

```sh
TIE_MACOS_SIGN_IDENTITY="Developer ID Application: Name (TEAMID)" \
TIE_MACOS_NOTARY_APPLE_ID="developer@example.com" \
TIE_MACOS_NOTARY_TEAM_ID=TEAMID \
TIE_MACOS_NOTARY_PASSWORD="app-specific-password" \
./packaging/macos/build-package.sh
```

The script signs embedded Mach-O libraries before the application and enables
the hardened runtime for Developer ID builds. Notarization runs in two passes:
the application is submitted and stapled first, then the DMG containing that
application is signed, submitted, and stapled.

## Signing a CI-built package

No signing material is stored on GitHub. The release workflow attaches an
ad-hoc-signed DMG to a draft GitHub release. A maintainer can re-sign and
notarize that exact artifact locally, then replace the release asset:

```sh
gh release download v1.2.3 --pattern 'opentie-*-macos-arm64-*.dmg' \
  --dir /tmp/opentie-package
TIE_MACOS_SIGN_IDENTITY="Developer ID Application: Name (TEAMID)" \
TIE_MACOS_NOTARY_PROFILE=TIE_NOTARY \
TIE_MACOS_RELEASE_TAG=v1.2.3 \
./packaging/macos/sign-package.sh \
  /tmp/opentie-package/opentie-1.2.3-macos-arm64-release.dmg
```

Run the same command for each DMG when signing multiple variants. The signing
script preserves a Debug package's dSYM, rebuilds the DMG in place, and uses
`gh release upload --clobber` when `TIE_MACOS_RELEASE_TAG` is set. Supplying a
second path to `sign-package.sh` writes a separate output DMG instead.

## Icons

The bundle embeds `cmake/macos/TIE.icns`, referenced by `CFBundleIconFile` in
`cmake/macos/Info.plist.in`.

## DMG Finder layout (optional)

By default the DMG opens as a plain Finder window. To ship a styled window,
add pre-baked Finder metadata:

```text
packaging/macos/dmg/DS_Store            copied to /.DS_Store in the volume
packaging/macos/dmg/background/         copied to /.background/ in the volume
```

Both are optional and applied only when present. The stored layout must be
authored for a volume named `OpenTIE`, matching the build script.

## Continuous integration

`.github/workflows/ci.yml` builds this package on GitHub-hosted Apple silicon
runners with ad-hoc signing and caches the pinned dependencies. On version
tags, `.github/workflows/release.yml` attaches the ad-hoc DMG to a draft GitHub
release. Releases can then be finalized locally with `sign-package.sh`.
