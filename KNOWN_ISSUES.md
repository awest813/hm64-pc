# HM64 PC Fork — Known Issues

This list tracks currently known limitations for public testing.

## Build / Setup

- Recomp dependencies (`tools/n64recomp`, `recomp/lib/N64ModernRuntime`, `recomp/lib/RT64`) are vendored
  into the repository, so no submodules or network fetches are required for a fresh clone/ZIP download.
  - Mitigation: if files are missing/corrupt, run `make recomp-deps` to validate them (it no longer downloads).
- Some minimal Linux images may have incomplete C++ linker setup (`-lstdc++` failures during CMake configure).
  - Mitigation: install full C++ toolchain (`g++` / libstdc++ dev packages), then run `make doctor`.
- Missing SDL2 headers will fail PC build configuration.
  - Mitigation: install `libsdl2-dev` (Ubuntu/WSL) and rerun `make doctor`.

## Runtime / Gameplay

- Public testing is still early; full gameplay stability/progression validation is in progress.
- Audio backend is implemented but reliability and edge cases are still under active testing.
  - Audio latency can drift during extended sessions if the host system is briefly under load;
    a queue-size cap (≈250 ms) is in place to throttle excess buffering.
- In-game rendering correctness is under active validation; see the **Rendering** section below
  for specific known categories.

## Rendering

The following rendering behaviors are known areas of potential divergence between the original
N64 hardware and the RT64-backed PC renderer.  Report any visual artefacts with a screenshot
and the in-game location/situation where they appear.

- **Texture filtering**: RT64 may apply bilinear filtering where the N64 hardware used
  nearest-neighbor, which can make low-resolution sprites appear blurry.  Compare with
  original hardware or an accurate N64 emulator if in doubt.
- **Alpha blending / coverage**: N64 coverage-based anti-aliasing and alpha compare thresholds
  may not match perfectly.  Sprite edges or transparent objects could render with minor
  fringing or clipping differences.
- **Framebuffer effects**: Any game code that reads back from the framebuffer (e.g. screen-
  fade or palette-shift effects) may not work correctly because RT64 does not fully emulate
  the N64 VI framebuffer-read path.
- **Fog**: Distance fog is supported by F3DEX2 / RT64, but color accuracy at fog boundaries
  may differ slightly from hardware.
- **2D/3D Z-ordering**: Depth-buffer interaction between 2D overlay sprites and 3D scene
  geometry could produce Z-fighting artefacts in edge cases.

## Input / UX

- No in-game remapping UI yet (keyboard/gamepad mappings are currently code-defined defaults).
- No dedicated user settings config file yet.
- Controller vibration (Rumble Pak) is not implemented; the N64 had no built-in rumble, but
  HM64 supports a Rumble Pak accessory that is not emulated in this PC port.

## Saves

- Save file is currently `hm64.sav` in the current working directory.  The startup log now
  prints the absolute path so you can locate it easily.
- Per-user platform-specific save directories (XDG on Linux, AppData on Windows) are planned,
  not yet implemented.

## Features Not Yet Implemented

The following PC-port features are planned but absent in the current build:

| Feature | Notes |
|---|---|
| User-configurable keybinds | Mappings are code-defined; no config file yet |
| User-configurable gamepad layout | Same as above |
| Per-user save directory | `hm64.sav` always written to cwd |
| Runtime settings file | No INI / TOML user config |
| Resolution / aspect-ratio options | Window is fixed at 2× (640×480); resizable but no UI |
| Widescreen support | Original 4:3 ratio only |
| Japanese version | `Makefile.jp` scaffold exists; recomp pipeline not yet validated for JP ROM |
| Controller Pak (Memory Card) | Stubbed as absent; SRAM is the only save path used |
| Audio volume control | No master or per-channel volume slider |
| Automated CI / regression tests | Planned; not yet configured |

---

If you hit a problem not listed here, please file a bug report with:
- build/run commands
- platform details
- reproduction steps
- logs/screenshots where relevant.
