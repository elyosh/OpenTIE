# OpenTIE

[![GitHub Release](https://img.shields.io/github/v/release/elyosh/OpenTIE)](https://github.com/elyosh/OpenTIE/releases/latest)
[![Visit our Discord server](https://img.shields.io/discord/1533001488391995442)](https://discord.gg/vfPVFxBA4)

![Screenshot of OpenTIE](assets/opentie-001.jpg)

OpenTIE is an open-source reimplementation of *Star Wars: TIE Fighter* for
Windows, macOS, and Linux. It runs the original game data natively on current
systems and supports both the 1995 Collector's CD-ROM and the 1998 Windows
release.

With both editions installed, OpenTIE can combine the 1995 menus, cutscenes,
and adaptive iMUSE soundtrack with the 1998 flight simulation and 3D assets.

> [!IMPORTANT]
> OpenTIE does not include any content from the original game. A complete
> installation of at least one supported edition is required. Install both
> editions to use the recommended combination.
>
> *Star Wars: TIE Fighter Special Edition* is available from
> [GOG](https://www.gog.com/en/game/star_wars_tie_fighter_special_edition) and
> [Steam](https://store.steampowered.com/app/355250/STAR_WARS_TIE_Fighter_Special_Edition/).

## The best of both editions

The 1995 and 1998 releases each have their own presentation, flight simulation,
and soundtrack. OpenTIE lets the menus and cutscenes, flight simulation, and
music be selected independently.

The recommended configuration combines:

- the 1995 menus and cutscenes
- the 1998 flight simulation and 3D assets
- the adaptive 1995 iMUSE soundtrack

This retains the 1998 flight presentation without giving up the interactive
music of the Collector's CD-ROM. Either supported edition can also be played
on its own.

## Adaptive music

OpenTIE reimplements the adaptive iMUSE soundtrack from the 1995 Collector's
CD-ROM. The music responds to mission events and moves seamlessly between
themes during flight.

OpenTIE can emulate Roland SC-55 Sound Canvas hardware, allowing iMUSE to use
the Sound Canvas version of the soundtrack while preserving its dynamic
transitions. This requires compatible SC-55 ROMs supplied by the user.

Built-in Sound Blaster-compatible OPL3 emulation is available for the classic
FM-synth sound, while FluidSynth can play the soundtrack with a user-selected
SoundFont.

The original prerecorded TIE98 music remains available when the complete 1998
experience is preferred.

## Graphics

OpenTIE offers classic and modern graphics modes. Classic mode preserves the
original presentation, while modern mode combines the original models and
cockpit artwork with high-resolution rendering and substantially more advanced
lighting.

Modern graphics include:

- shadows, ambient occlusion, and bloom
- motion blur
- MSAA and AMD FidelityFX FSR 3.1.4 anti-aliasing and upscaling
- HDR output

Press `TAB` while the game is running to switch between classic and modern
graphics.

## Smoother flight

Flight simulation can update at up to 240 Hz to match modern high-refresh-rate
displays, providing smoother motion and more responsive controls. The original
TIE95 and TIE98 timing modes remain available when their exact behavior is
preferred.

Modern gamepads and joysticks are supported and can be configured from
OpenTIE's settings.

## Getting started

1. Download the latest package for your platform from
   [GitHub Releases](https://github.com/elyosh/OpenTIE/releases/latest).
2. Extract or install the package and launch OpenTIE.
3. Select the complete TIE95 and/or TIE98 installation folders when prompted.
4. If both editions are available, select the recommended configuration to
   combine the 1995 presentation and music with the 1998 flight experience.

OpenTIE validates the selected installations, remembers them for future
launches, and imports compatible existing pilot files during initial setup.

To access OpenTIE settings from the concourse, press `ESC` and select
**OpenTIE Settings**. During flight, press `ESC` to open them directly.

Useful shortcuts:

| Key | Action |
|---|---|
| `TAB` | Switch between classic and modern graphics |

## Supported platforms

| Platform | Target | Graphics backend |
|---|---|---|
| Windows | x86-64 | Direct3D 12 or Vulkan |
| macOS | macOS 13 or later; arm64 or x86-64 | Metal |
| Linux | x86-64; glibc 2.35 or later | Vulkan |

## Current state

OpenTIE remains under active development. Bugs and differences from the
original releases are still possible.

## Community

Join the [TotallyOpen Discord server](https://discord.gg/vfPVFxBA4) to discuss
OpenTIE, OpenXWA, development, and the Totally Games flight simulators.

## System requirements

- a 64-bit system with a modern GPU
- a complete installation of the 1995 Collector's CD-ROM or the 1998 Windows
  release

Release packages include the required runtime libraries. Keep the executable,
libraries, resources, and shader directories together when moving an
installation.

## Building from source

The build requires CMake 3.23 or later, a C/C++ toolchain, SDL3 3.4, zstd,
FFmpeg, and SDL_shadercross. Release packaging pins its dependencies, including
SDL3 3.4.14, and provides the reference for reproducible builds.

Platform-specific instructions are available for
[Windows](packaging/windows/README.md),
[macOS](packaging/macos/README.md), and
[Linux](packaging/linux/README.md).
