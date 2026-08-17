# Harvest Moon 64 PC Fork

> **A PC-focused Harvest Moon 64 fork built from decomp/recomp work, aimed at faithful play with respectful quality-of-life improvements.**

This project exists to bring **Harvest Moon 64** to PC in a way that is:
- easy to build
- easy to test
- easy to play
- true to the spirit of the original game

It is **not** a remake and it is **not** trying to redesign HM64’s identity.

---

## Project Philosophy

> **Bring Harvest Moon 64 to PC with respectful quality-of-life improvements, while staying true to the spirit, pacing, atmosphere, and heart of the original game.**

What that means in practice:
- prioritize preservation and faithfulness
- ship practical convenience and accessibility improvements
- keep gameplay-altering changes optional (or avoid them)
- avoid features that clash with HM64’s tone, progression, or charm

---

## What This Fork Is

- Based on the HM64 US decompilation and N64Recomp workflow
- Builds a native PC executable (`hm64_pc`) using N64ModernRuntime
- Focused on tester-friendly iteration and practical PC usability

See [recomp/README.md](recomp/README.md) for port internals.

---

## Current Status (Honest Snapshot)

> ⚠️ Public testing stage: playable foundations exist, but this is still early.

| Area | Status |
|---|---|
| US decompilation | ✅ Complete |
| Native PC boot path | ✅ Working |
| Title/menu rendering path | ✅ Working |
| Keyboard controls | ✅ Working |
| Gamepad controls | ✅ Working (includes C-button equivalents) |
| Save file backend (`hm64.sav`) | ✅ Working |
| Audio backend (SDL2) | ✅ Implemented, reliability still being tested |
| Full gameplay stability/progression validation | 🔧 In progress |
| JP port parity | 📋 Planned |
| Advanced PC settings UI/config | 📋 Planned |

For full priorities and status, see [ROADMAP.md](ROADMAP.md).

---

## Platform Support

| Platform | Status | Notes |
|---|---|---|
| Windows (WSL2) | ✅ Recommended | Primary documented path for Windows users |
| Linux (native) | ✅ Supported | Use distro equivalents for package names |
| macOS | 📋 Not currently documented | Contributions welcome |

---

## ROM / Legal Requirement

You must provide your own legally obtained **US ROM**:

- Filename: `baserom.us.z64`
- Format: big-endian Z64
- SHA-1: `90631460f1876a14849df0541d534012b410a34c`

This repository does not and cannot distribute game ROM data.

---

## Quick Start (First-Time Testers)

### 1) Clone

```sh
git clone https://github.com/awest813/hm64-pc.git
cd hm64-pc
```

> All build dependencies are **vendored directly into this repository**:
> `tools/n64recomp`, `recomp/lib/N64ModernRuntime`, and `recomp/lib/RT64`
> (including the HM64-specific HLE/title rendering fixes), plus the
> GCC 2.7.2 / binutils 2.6 N64 toolchain (`tools/gcc-2.7.2`) and all pinned
> Python wheels (`tools/python-wheels`). A fresh clone or GitHub ZIP download
> contains everything except the game ROM — no submodules, no extra network
> fetches.

### 2) Bootstrap tools/deps

Ubuntu/WSL (recommended):

```sh
tools/setup.sh --install-system-deps
```

If your system packages are already installed:

```sh
tools/setup.sh
```

### 3) Verify recomp runtime/tool dependencies (vendored, no download)

```sh
make recomp-deps
```

### 4) Place ROM in repo root

```sh
cp /path/to/baserom.us.z64 .
```

### 5) Validate environment

```sh
make doctor
```

### 6) Build PC test binary (full pipeline)

```sh
make pc
```

### 7) Run

```sh
./recomp/build/hm64_pc
```

Or pass ROM path explicitly:

```sh
./recomp/build/hm64_pc /path/to/baserom.us.z64
```

---

## Build Commands Reference

Run `make help` for a built-in summary.

| Command | Purpose |
|---|---|
| `make doctor` | Preflight checks (toolchain, Python deps, ROM, SDL2, runtime deps) |
| `make setup` | Split/extract required assets from ROM |
| `make` | Build and diff-match `hm64.z64` against `baserom.us.z64` |
| `make recomp-deps` | Ensure N64Recomp + N64ModernRuntime sources are available |
| `make recomp-generate` | Generate recompiled C files (`recomp/output/funcs`) |
| `make recomp-build` | Build `recomp/build/hm64_pc` from generated output |
| `make recomp` | Full recomp pipeline |
| `make pc` | Recommended one-command first-time PC build flow |

Debug build (manual CMake path):

```sh
cmake -S recomp -B recomp/build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build recomp/build-debug --parallel
```

---

## First-Run Success Checklist

After a successful first run you should see:
1. `hm64_pc` launches a resizable window
2. title/menu flow appears
3. keyboard input works (`Enter`, movement keys, etc.)
4. gamepad input works if controller is connected
5. save file `hm64.sav` is created on first in-game save

If anything in this list fails, check [Troubleshooting](#troubleshooting) and [TESTING.md](TESTING.md).

---

## Controls

### Keyboard Defaults

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

### Gamepad Defaults (SDL2 GameController)

- Left stick → N64 analog stick
- D-pad → N64 D-pad
- A/B/Start/L/R/Z mapped from standard pad buttons/triggers
- **C-button equivalents**:
  - Right stick directions → C buttons
  - Y → C-Up
  - X → C-Left
- Hot-plugging supported (connect/disconnect while running)

---

## Save and Config Behavior

- Save file: `hm64.sav`
- Location: current working directory (usually next to `hm64_pc` when launched from repo root)
- Created automatically on first save
- Delete `hm64.sav` to reset cartridge-style save data

There is currently no separate end-user config file for keybinds/settings.

---

## Troubleshooting

### `make doctor` fails

Run the suggested fix path shown by doctor, typically:

```sh
tools/setup.sh --install-system-deps
make recomp-deps
# place baserom.us.z64 in repo root
make doctor
```

### `make` fails with missing ROM

You must place `baserom.us.z64` in the repository root, or use the runtime CLI ROM argument when launching `hm64_pc`.

### `make recomp-build` fails with C++/`-lstdc++` linker errors

Install a full C++ toolchain (`g++` / libstdc++ development packages), then rerun `make doctor`.

### `SDL2` not found during CMake

Install SDL2 development headers (`libsdl2-dev` on Ubuntu/WSL).

### `output/funcs` is empty

Run:

```sh
make recomp-generate
```

before `make recomp-build`.

### Controller issues

- Try unplug/replug while running (hot-plug supported)
- Ensure controller is recognized by SDL2
- Drift can be adjusted in `recomp/platform/input.cpp` via `GAMEPAD_DEAD_ZONE`

---

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for full guidance.

Current high-value contribution areas:
- gameplay/regression testing and high-quality bug reports
- audio stability and correctness
- PC UX quality-of-life that remains faithful to HM64
- decomp cleanup/research (`FIXME` cleanup, naming/documentation improvements)

Design rule of thumb: if a change makes HM64 feel like a different game, it does not belong in this fork.

---

## Tester Workflow and Reporting

- Start here: [TESTING.md](TESTING.md)
- Known open issues and test gaps: [KNOWN_ISSUES.md](KNOWN_ISSUES.md)
- Use GitHub issue templates for bug reports / feature requests

---

## Legal

Harvest Moon 64 is © 1999 Victor Interactive Software / Marvelous Entertainment.

This project does not distribute ROM content. Use your own legally obtained dump.
