# HM64 PC Port – n64recomp Integration

This directory contains the build infrastructure for running Harvest Moon 64
natively on PC using [N64: Recompiled](https://github.com/N64Recomp/N64Recomp)
(n64recomp) for static MIPS→C recompilation.

## Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│  Decomp build  →  hm64.elf  →  n64recomp  →  recomp/output/funcs/  │
│                                                        │            │
│  recomp/platform/  ──────────────────────────┐         │            │
│  recomp/patches/   ──────────────────────────┼──► hm64_pc (binary)  │
│  N64ModernRuntime (ultramodern + librecomp)  ┘                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Components

| Directory / File             | Purpose                                          |
|------------------------------|--------------------------------------------------|
| `hm64.us.toml`               | N64Recomp configuration (stubs, output paths)    |
| `CMakeLists.txt`             | CMake build for the PC executable                |
| `platform/main.cpp`          | Host `main()`, ultramodern initialisation        |
| `platform/audio.{h,cpp}`     | SDL2 audio backend                               |
| `platform/input.{h,cpp}`     | SDL2 keyboard + gamepad backend                  |
| `platform/graphics.{h,cpp}`  | SDL2 window backend (RT64 via ultramodern)        |
| `patches/nusys_patches.cpp`  | PC replacements for NuSystem API stubs           |
| `patches/rdp_patches.cpp`    | RDP-related patches (currently empty)            |
| `patches/rsp_audio_patches.cpp` | RSP audio patches (currently empty)           |
| `patches/sram_patches.cpp`   | PC-native SRAM save file (file I/O backend)      |
| `patches/title_patches.cpp`  | Title/menu screen – primary test target          |
| `lib/N64ModernRuntime/`      | Runtime library (git submodule)                  |
| `output/`                    | Generated C files from n64recomp (build artifact)|
| `build/`                     | CMake build tree (build artifact)                |

## Prerequisites

```sh
# System packages
sudo apt install -y build-essential cmake ninja-build \
                    libsdl2-dev clang

# Git submodules (run from repo root)
git submodule update --init --recursive
```

## Build

> **Step 1**: Build the decomp to produce `hm64.elf`
> (requires a ROM at `baserom.us.z64`)

```sh
make setup && make
```

> **Step 2**: Run n64recomp to generate per-function C files

```sh
make recomp-generate
```

> **Step 3**: Compile the PC port

```sh
make recomp-build
```

Or do all three in one go:

```sh
make recomp
```

The resulting binary is at `recomp/build/hm64_pc`.

### Using CMake presets (alternative to `make recomp-build`)

A `CMakePresets.json` is provided in this directory for developers who prefer
working directly with CMake.

```sh
# Release build (default for testers)
cmake --preset release -S recomp
cmake --build --preset release

# Debug build (with symbols, for developers)
cmake --preset debug -S recomp
cmake --build --preset debug
```

Binaries land in `recomp/build/hm64_pc` (release) or
`recomp/build-debug/hm64_pc` (debug).

## Running

```sh
./recomp/build/hm64_pc [path/to/baserom.us.z64]
```

If no ROM path is given, the binary looks for `baserom.us.z64` in the current
working directory.

## Testing – Menu Rendering

The first test target is the **title screen** (`src/game/title.c`).  The
relevant functions are:

| Function                   | What it does                                      |
|----------------------------|---------------------------------------------------|
| `initializeTitleScreen()`  | Sets up sprites, positions, and animations        |
| `updateTitleScreen()`      | Handles "Press Start" blink and dog animation     |
| `renderTitleScreen()`      | Builds the display list and submits a GFX task    |

Expected result: the game window shows the *Harvest Moon 64* logo, copyright
text, and the "Press Start / How to Play" menu.

### Default keyboard controls

| Key(s)          | N64 button        |
|-----------------|-------------------|
| Enter           | Start             |
| Z               | Z trigger         |
| X               | B button          |
| C               | A button          |
| Shift           | R trigger         |
| Q               | L trigger         |
| Arrow keys      | D-Pad             |
| W / A / S / D   | Analog stick      |
| I / J / K / L   | C-Up / C-Left / C-Down / C-Right |
| Escape          | Quit              |
| F11             | Toggle fullscreen |

A connected gamepad is also supported automatically (first detected device).
Left-stick dead zone is ~10 % of full range to prevent drift.

## RDP / RSP

Graphics (RDP) commands are processed by
[RT64](https://github.com/rt64/rt64) via ultramodern.

Audio (RSP n_aspMain microcode) is handled by ultramodern's built-in audio
mixer, which outputs 16-bit stereo PCM at 32 kHz to the SDL2 audio device.

## Adding Patches

When a function behaves differently on PC (e.g., a hardware-specific check or
a timing-dependent loop), add a `RECOMP_PATCH` function in one of the
`patches/` files.  The linker will automatically prefer your patch over the
recompiled version because of how N64Recomp's single-file output mode
interacts with the linker.

See `patches/title_patches.cpp` for an example (`mainproc` replacement).

## Save Files

Game progress is saved in `hm64.sav` (placed next to the binary, or in the
current working directory).  It mirrors the 32 KB cartridge SRAM layout:

| Offset range     | Contents                           |
|------------------|------------------------------------|
| 0x0000 – 0x0FFF  | Save slot 1 (4 KB)                 |
| 0x1000 – 0x1FFF  | Save slot 2 (4 KB)                 |
| 0x2000 – 0x2FFF  | Save slot 3 (4 KB)                 |
| 0x3000 – 0x3FFF  | Save slot 4 (4 KB)                 |
| 0x4000 – 0x7FFF  | Farm-ranking data (16 KB)          |

The file is created automatically on first save.  Delete `hm64.sav` to reset
all saved data to a blank state (equivalent to removing the cartridge battery).

## Troubleshooting

| Symptom                              | Likely cause / fix                                           |
|--------------------------------------|--------------------------------------------------------------|
| Black screen, game thread hung       | `nuGfxFuncSet` not storing callback; check nusys_patches     |
| Black screen, no crash               | RDP tasks not submitted; check nusys_patches                 |
| Shows "No Controller" at startup     | `nuContInit` not returning 1; check nusys_patches            |
| Crash in DMA path                    | ROM DMA issue; add a patch in librecomp config               |
| No audio                             | SDL2 audio device init failed; check audio.cpp               |
| Controller not responding            | Check input.cpp; verify SDL2 gamepad mapping                 |
| Analog stick drifts                  | Dead zone too small; adjust GAMEPAD_DEAD_ZONE in input.cpp   |
| Save/load does nothing               | Check sram_patches.cpp; verify hm64.sav is writable          |
| Build fails: missing submodule       | Run `git submodule update --init --recursive`                |
| Build fails: missing funcs dir       | Run `make recomp-generate` first                             |
| Window too small                     | Press F11 to toggle fullscreen                               |
| ROM not found on launch              | Pass ROM path as argument: `./hm64_pc path/to/baserom.us.z64`|
