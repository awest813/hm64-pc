# Harvest Moon 64 – PC Port

> **A faithful Harvest Moon 64 PC port** built on a complete N64 decompilation,
> with tasteful quality-of-life improvements that respect the original game's
> spirit, pacing, and charm.

---

## What is this?

This is a PC-native port of **Harvest Moon 64** (US version) derived from the
[HM64 decompilation project](https://github.com/harvestwhisperer/hm64-decomp).
The entire game has been statically recompiled from MIPS to C using
[N64Recomp](https://github.com/N64Recomp/N64Recomp), then rebuilt as a native
PC executable using the [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
(ultramodern + RT64 + librecomp).

**This fork is not a remake.** It aims to run the original game faithfully on
modern hardware while adding conveniences that make it more comfortable to play —
widescreen-friendly rendering, keyboard/gamepad support, easy saving, and a
native window — without touching pacing, mechanics, or the heart of what makes
HM64 special.

---

## Philosophy

> *Bring Harvest Moon 64 to PC with respectful quality-of-life improvements,
> while staying true to the spirit, pacing, atmosphere, and heart of the
> original game.*

Every change in this fork should feel like it belongs. If a feature would
make HM64 feel like a different game, it does not belong here.

---

## Current Status

> ⚠️ **Early testing.** The project is actively being developed. Expect rough
> edges.

| Area                        | Status                            |
|-----------------------------|-----------------------------------|
| Decomp (US)                 | ✅ 100% complete                  |
| Title screen / main menu    | ✅ Renders correctly              |
| Keyboard input              | ✅ Working                        |
| Gamepad input               | ✅ Working (first detected device) |
| Save / load                 | ✅ Implemented (`hm64.sav`)       |
| SDL2 audio backend          | ✅ Implemented                    |
| Gameplay (past title screen)| 🔧 In progress / unstable         |
| Audio reliability           | 🔧 In progress                    |
| Japan version               | 🔧 Basic scaffolding only         |
| Widescreen / resolution     | 📋 Planned                        |
| Settings / config file      | 📋 Planned                        |

See [ROADMAP.md](ROADMAP.md) for the full breakdown.

---

## Quick Start

### Requirements

You need to supply your own legally obtained `baserom.us.z64` ROM dump
(big-endian / Z64 format, SHA-1: `90631460f1876a14849df0541d534012b410a34c`).
The project cannot distribute ROM data.

### Windows (via WSL2) — Recommended for Windows users

1. **Install WSL2** – [Microsoft guide](https://learn.microsoft.com/en-us/windows/wsl/install)

2. **Inside WSL**, install system packages:
   ```sh
   sudo apt-get update
   sudo apt install -y \
     build-essential cmake ninja-build clang \
     libsdl2-dev \
     python3 python3-pip \
     binutils-mips-linux-gnu gcc-mips-linux-gnu wget
   ```

3. **Clone the repository** (with submodules):
   ```sh
   git clone --recursive https://github.com/awest813/hm64-pc.git
   cd hm64-pc
   ```

4. **Run the setup script** (installs Python deps, GCC 2.7.2, Splat):
   ```sh
   chmod +x tools/setup.sh
   sudo tools/setup.sh
   ```

5. **Place your ROM** in the project root:
   ```sh
   cp /path/to/baserom.us.z64 .
   ```

6. **Build everything** (ROM → recomp → PC binary):
   ```sh
   make setup && make recomp
   ```

7. **Run the game:**
   ```sh
   ./recomp/build/hm64_pc
   ```

### Linux (native)

Same steps as WSL2 above, but skip step 1 and use your distro's package manager.
On Arch-based systems, substitute `libsdl2` for `libsdl2-dev` and
`mipsel-linux-gnu-gcc` may differ by distro — check your package repos.

---

## Build Reference

| Command                  | What it does                                               |
|--------------------------|------------------------------------------------------------|
| `make setup && make`     | Build the decomp and produce `build/hm64.elf` + `hm64.z64`|
| `make recomp-generate`   | Run n64recomp on the ELF → generates `recomp/output/funcs/`|
| `make recomp-build`      | Configure + compile the PC executable                      |
| `make recomp`            | All three recomp steps in one command                      |
| `VERBOSE=1 make`         | Show full compiler output during decomp build              |

The final binary is at **`recomp/build/hm64_pc`**.

#### Debug build

```sh
cmake -S recomp -B recomp/build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build recomp/build-debug --parallel
```

---

## Controls

### Keyboard (default)

| Key(s)          | N64 button                          |
|-----------------|-------------------------------------|
| Enter           | Start                               |
| Z               | Z trigger                           |
| X               | B button                            |
| C               | A button                            |
| Shift           | R trigger                           |
| Q               | L trigger                           |
| Arrow keys      | D-Pad                               |
| W / A / S / D   | Analog stick                        |
| I / J / K / L   | C-Up / C-Left / C-Down / C-Right    |
| F11             | Toggle fullscreen                   |
| Escape          | Quit                                |

### Gamepad

A connected gamepad (Xbox, PlayStation, etc.) is detected automatically.
Standard button mappings apply. Left-stick dead zone is ~10% to prevent drift.

---

## Save Files

Progress is saved in **`hm64.sav`**, created automatically on first save.
The file is placed next to the `hm64_pc` binary (or in the working directory
when launching from elsewhere).

| Offset           | Contents                  |
|------------------|---------------------------|
| 0x0000 – 0x0FFF  | Save slot 1 (4 KB)        |
| 0x1000 – 0x1FFF  | Save slot 2 (4 KB)        |
| 0x2000 – 0x2FFF  | Save slot 3 (4 KB)        |
| 0x3000 – 0x3FFF  | Save slot 4 (4 KB)        |
| 0x4000 – 0x7FFF  | Farm-ranking data (16 KB) |

Delete `hm64.sav` to reset all saves (equivalent to removing the cartridge
battery on original hardware).

---

## Troubleshooting

| Symptom                            | Likely cause / fix                                             |
|------------------------------------|----------------------------------------------------------------|
| Black screen, game thread hung     | `nuGfxFuncSet` callback issue — check `recomp/patches/nusys_patches.cpp` |
| Black screen, no crash             | RDP tasks not submitted — check nusys_patches                  |
| "No Controller" at startup         | `nuContInit` not returning 1 — check nusys_patches             |
| No audio / audio crackling         | SDL2 audio device init failed — check `recomp/platform/audio.cpp` |
| Gamepad not detected               | Check `recomp/platform/input.cpp`; try unplugging and replugging |
| Analog stick drifts                | Adjust `GAMEPAD_DEAD_ZONE` in `recomp/platform/input.cpp`     |
| Save/load does nothing             | Check that `hm64.sav` is writable in the binary's directory   |
| Build fails: missing submodule     | Run `git submodule update --init --recursive`                  |
| Build fails: `output/funcs/` empty | Run `make recomp-generate` before `make recomp-build`          |
| ROM not found on launch            | Pass ROM path: `./hm64_pc path/to/baserom.us.z64`              |
| Window too small                   | Press **F11** to toggle fullscreen                             |
| `cmake: command not found`         | Install cmake 3.20+: `sudo apt install cmake`                  |
| `SDL2 not found` during cmake      | Install SDL2 dev package: `sudo apt install libsdl2-dev`       |

---

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for
full guidelines. Quick summary:

**High-priority areas right now:**
- Testing and reporting gameplay bugs past the title screen
- Improving audio stability and playback accuracy
- Cleaning up fake/forced matches in decomp code (search `FIXME`)
- Researching function, struct, and variable purposes for better labels
- JP version matching (only scaffolding exists)

**Design principles for this fork:**
- Changes should feel at home in the original game
- Quality-of-life improvements must be non-invasive
- No feature that would change the core feel, pacing, or progression
- Bug fixes that improve the experience without altering identity are always welcome

For decomp.me matching work, select the **Harvest Moon 64** compiler preset
when creating a new scratch.

---

## Project Structure

```
hm64-pc/
├── src/          # Decompiled game source (~92k lines of C)
├── recomp/       # PC port (n64recomp integration, SDL2 backends, patches)
│   ├── platform/ # SDL2 window / audio / input backends + main()
│   ├── patches/  # RECOMP_PATCH overrides for PC-specific behaviour
│   └── lib/      # N64ModernRuntime submodule (ultramodern + librecomp)
├── tools/        # Build tools and asset extraction scripts
├── assets/       # Extracted sprites, textures, maps, animations
├── config/       # Region-specific linker and build configuration
└── lib/          # N64 SDK libraries (libultra, nusys, etc.)
```

See [`recomp/README.md`](recomp/README.md) for detailed PC port internals.

---

## Decomp Notes (for contributors)

The US version is **100% decompiled**, including:
- All game functions, data, and rodata
- All library functions (libultra, NuSystem)
- Cutscene DSL (compiles to game bytecode)
- Dialogue DSL (compiles to game bytecode)
- Automatic text extraction and transpilation

The build also supports **shiftability** (for modding). See the `dev` or
`dev-qol` branch for modding-friendly starting points.

---

## Legal

This project does not include and cannot distribute ROM data.
You must provide your own legally obtained `baserom.us.z64`.

Harvest Moon 64 is © 1999 Victor Interactive Software / Marvelous Entertainment.

---

## Roadmap

See [ROADMAP.md](ROADMAP.md) for current priorities and planned work.
