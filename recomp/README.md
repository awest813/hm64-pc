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
