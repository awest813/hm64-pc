# HM64 PC Fork — Known Issues

This list tracks currently known limitations for public testing.

## Build / Setup

- In some branch snapshots, recomp dependencies may not be available as active git submodules.
  - Mitigation: run `make recomp-deps` (uses submodule init when possible, clone fallback otherwise).
- Some minimal Linux images may have incomplete C++ linker setup (`-lstdc++` failures during CMake configure).
  - Mitigation: install full C++ toolchain (`g++` / libstdc++ dev packages), then run `make doctor`.
- Missing SDL2 headers will fail PC build configuration.
  - Mitigation: install `libsdl2-dev` (Ubuntu/WSL) and rerun `make doctor`.

## Runtime / Gameplay

- Public testing is still early; full gameplay stability/progression validation is in progress.
- Audio backend is implemented but reliability and edge cases are still under active testing.

## Input / UX

- No in-game remapping UI yet (keyboard/gamepad mappings are currently code-defined defaults).
- No dedicated user settings config file yet.

## Saves

- Save file is currently `hm64.sav` in the current working directory.
- Per-user platform-specific save directories (XDG/AppData) are planned, not yet implemented.

---

If you hit a problem not listed here, please file a bug report with:
- build/run commands
- platform details
- reproduction steps
- logs/screenshots where relevant.
