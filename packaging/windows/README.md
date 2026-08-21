# Windows build with GCC MinGW-w64

This Docker build produces a self-contained x86-64 OpenTIE package using GCC
MinGW-w64. SDL3, zstd, FFmpeg, FluidSynth, SpeexDSP, and the required shader
tools are downloaded and built inside Docker; they are not vendored in this
repository or required on the host.

The build requires the Docker Buildx plugin.

## Build a release package

Run from the repository root:

```sh
docker buildx build \
  --file packaging/windows/Dockerfile \
  --build-arg TIE_BUILD_TYPE=Release \
  --target artifact \
  --output type=local,dest=build/artifacts \
  .
```

The output is:

```text
build/artifacts/opentie-0.0.0-dev-windows-x86_64-release.zip
```

## Build a debug package

```sh
docker buildx build \
  --file packaging/windows/Dockerfile \
  --build-arg TIE_BUILD_TYPE=Debug \
  --target artifact \
  --output type=local,dest=build/artifacts \
  .
```

The output is:

```text
build/artifacts/opentie-0.0.0-dev-windows-x86_64-debug.zip
```

`TIE_BUILD_TYPE` defaults to `Release` and accepts only `Release` or `Debug`.
Debug packages contain MinGW DWARF debug information, enable the in-game debug
UI, and include WinPixEventRuntime for GPU debug labels. Release packages
disable the debug UI and omit that runtime.

`TIE_VERSION` sets the version in the package name and defaults to
`0.0.0-dev`. CI passes the short commit hash for development builds and the
tag version for releases (`--build-arg TIE_VERSION=1.2.3`).

The pinned third-party libraries and build-host tools are built in Release
mode for both configurations. Only OpenTIE changes configuration, keeping
dependency layers reusable between release and debug package builds.

After extraction, the package contains:

```text
opentie-0.0.0-dev-windows-x86_64-release/
├── OpenTIE.exe
├── *.dll
├── licenses/
├── resources/
└── shaders/
```

The DLL set includes SDL3, the minimal FFmpeg runtime, zstd, FluidSynth,
SpeexDSP, and the GCC MinGW-w64 runtime libraries imported by `OpenTIE.exe`. On
first launch, the game asks for supported original TIE Fighter installation
directories and remembers the validated selections. Application resources are
resolved from the packaged `resources` directory; original game assets are
not included.

## Dependency versions

Dependency and shader-tool revisions are pinned as Docker build arguments in
the Dockerfile. Override a pin explicitly when testing an update:

```sh
docker buildx build \
  --file packaging/windows/Dockerfile \
  --build-arg SDL_VERSION=3.4.14 \
  --target artifact \
  --output type=local,dest=build/artifacts \
  .
```

## Icons

`OpenTIE.exe` embeds `cmake/windows/tie.ico` and a `VERSIONINFO` resource generated
from `cmake/windows/tie.rc.in`.

## Applied dependency patches

Patches under `packaging/common/` are applied to cloned sources before the
dependency builds. Recheck them when a pinned revision changes; `git apply`
fails the build if a patch no longer applies.

| Patch | Applies to | Purpose |
|-|-|-|
| `fluidsynth-library-only.patch` | FluidSynth | Allows building and installing the library without the command-line program |
| `shadercross-fsr3.patch` | SDL_shadercross (host) | FSR3 shader generation support |
| `sdl-gpu-d3d12-hdr.patch` | SDL3 (Windows target) | Lets the D3D12 SDL_GPU backend report and enable HDR swapchain compositions |
| `vcpkg-fluidsynth-library-only.patch` | vcpkg FluidSynth port | Selects the library-only build for OpenTIE's Windows dependency package |

SDL_shadercross is a build-host tool. The Dockerfile builds it for Linux and
uses it to generate DXIL and SPIR-V while compiling OpenTIE and its runtime
dependencies for Windows. The FSR shader generator is likewise built for the
Linux host before the MinGW-w64 target build.
