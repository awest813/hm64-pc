# HM64 PC Port — Recomp Integration Details

This directory contains the n64recomp + runtime integration layer for building
Harvest Moon 64 as a native PC executable.

## Pipeline overview

```
decomp build (hm64.elf)
      ↓
n64recomp codegen (recomp/output/funcs/*.c)
      ↓
CMake build (platform + patches + generated funcs)
      ↓
hm64_pc
```

## Key components

| Path | Purpose |
|---|---|
| `hm64.us.toml` | N64Recomp configuration and stub list |
| `CMakeLists.txt` | PC executable build |
| `platform/` | Host platform backends (window/audio/input/main) |
| `patches/` | PC-specific RECOMP_PATCH overrides |
| `lib/N64ModernRuntime/` | Runtime dependency (ultramodern + librecomp) |
| `output/funcs/` | Generated C from n64recomp |

## Prerequisites (repo root)

```sh
tools/setup.sh --install-system-deps
make recomp-deps
make doctor
```

`make doctor` is the fastest way to catch missing toolchain/ROM/runtime dependencies.

## Build (repo root)

```sh
make setup && make          # decomp + match check
make recomp-generate        # generate recomp/output/funcs
make recomp-build           # build recomp/build/hm64_pc
```

One-command full flow:

```sh
make recomp
```

First-time tester flow:

```sh
make pc
```

## Running

```sh
./recomp/build/hm64_pc [path/to/baserom.us.z64]
```

If no argument is passed, runtime defaults to `baserom.us.z64` in the current
working directory.

## Input defaults (runtime behavior)

### Keyboard

| Key(s) | N64 input |
|---|---|
| Enter | Start |
| Z | Z trigger |
| X | B |
| C | A |
| Shift | R trigger |
| Q | L trigger |
| Arrow keys | D-pad |
| W / A / S / D | Analog stick |
| I / J / K / L | C-Up / C-Left / C-Down / C-Right |
| F11 | Toggle fullscreen |
| Escape | Quit |

### Gamepad

- Left stick → analog stick (dead zone in `input.cpp`)
- D-pad / A / B / Start / shoulders mapped
- **Left or right trigger** → Z button (either trigger works)
- C-button equivalents:
  - Right stick directions
  - Y → C-Up
  - X → C-Left
- Hot-plugging supported (connect/disconnect while running)

## Save file behavior

- SRAM is persisted to `hm64.sav`
- Location: current working directory
- Layout mirrors original 32KB cartridge SRAM
- File is auto-created on first save

## RDP / RSP notes

- RDP graphics tasks are handled by ultramodern (RT64 path)
- RSP audio tasks are routed through ultramodern mixer to SDL2 audio output

## Troubleshooting

| Symptom | Likely fix |
|---|---|
| `make recomp-build` fails before configure | Run `make recomp-deps` |
| CMake C++ linker fails (`-lstdc++`) | Install `g++` / libstdc++ dev packages |
| `SDL2` not found | Install `libsdl2-dev` |
| Missing `output/funcs/` | Run `make recomp-generate` first |
| ROM not found | Pass ROM path arg or place `baserom.us.z64` in cwd |
| Controller drift | Adjust `GAMEPAD_DEAD_ZONE` in `platform/input.cpp` |

## Patch authoring notes

Use `RECOMP_PATCH` functions in `patches/` for behavior that must differ from
the original N64 hardware assumptions.

Keep patches focused and well-commented so faithfulness and maintainability are
easy to review.

## Startup investigation (September 2026)

Code generation now bundles ELF data into the executable and derives asset ROM
mappings from the same ELF and the US splat configuration. Run
`make recomp-generate` before `make recomp-build` after changing the ELF; generated
functions, symbol addresses, and data must be kept together. The executable no
longer opens `hm64.elf` at runtime.

The native path now submits initialization, scene, and final-sync display lists
using the generated camera and graphics routines. VI setup uses the correct mode
stride and scanline origin, and the retrace stack no longer overlaps game BSS.
SDL input is sampled on the main thread and exposed through NuSystem controller
status and extended reads. Controller edges are retained while the game processes
the previous tick. The original map, sprite transform, and fade routines now run
without forcing a jump to the title.

Cutscene DMA distinguishes original ROM offsets embedded in scripts from relocated
ELF asset addresses. Native rendering explicitly selects F3DEX2 and waits for each
display list to be parsed before its memory is reused.

The September 18 WSL build renders the startup logo, opening, animated title,
Play / How to Play menu, and Select a Diary screen through RT64. Press Enter
(Start) to skip the opening, Enter again at the title, and Enter or C (A) on Play
to reach diary selection. WASD controls menu movement. Short keyboard presses are
retained until a game tick reads them, including analog-stick keys. Drawing and
game updates are serialized to protect shared scene data during transitions.

The opening dialogue now scrolls through automatically. Map tile commands stay
within their stack allocations, preserving the registers used for NPC visibility.
Message timers use their actual six-byte, two-byte-aligned type, avoiding the
misaligned writes that stalled "Hey, have another drink" at its third line.

The silent audio backend completes sequence requests instead of leaving playback
flags set forever; opening cutscenes can therefore finish their audio waits.
Audio playback and gameplay beyond the menus remain unfinished. Shutdown also
needs work: a timed run exposed a game-thread access after RDRAM teardown.

Run the headless input regression check without a ROM:

```bash
cmake --build recomp/build --target hm64_input_test
SDL_VIDEODRIVER=dummy ./recomp/build/hm64_input_test
```

After generating the game code, run the rendering/dialogue regression check:

```bash
python3 tools/test_map_rendering.py
```

This runs the actual recompiled functions against synthetic memory: tile rendering
must preserve caller registers, and a two-byte-aligned dialogue timer must scroll
a line in 16 ticks. No ROM or captured game memory is loaded by the test.
