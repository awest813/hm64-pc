# HM64 PC Port – Roadmap

This roadmap reflects the actual current state of the project and its next
priorities. Status marks are honest — nothing is listed as done unless it is
done in the codebase.

**Status key:**
- ✅ Done
- 🔧 In progress / partially working
- 📋 Planned
- 💡 Optional / stretch goal

---

## 1. Core Playability

Goals: get the game running stably from boot through the first playable session.

| Item                                           | Status |
|------------------------------------------------|--------|
| US version 100% decompiled                     | ✅     |
| Static MIPS→C recompilation (n64recomp)        | ✅     |
| PC executable boots to title screen            | ✅     |
| Title screen renders correctly                 | ✅     |
| "Press Start / How to Play" menu flow          | ✅     |
| Controller input received at title screen      | ✅     |
| Save file write / read (`hm64.sav`)            | ✅     |
| Game boots past title into gameplay            | 🔧     |
| File select screen stability                   | 🔧     |
| In-game map rendering                          | 🔧     |
| NPC / character rendering                      | 🔧     |
| Gameplay loop (farming, time, seasons)         | 🔧     |
| Cutscene playback                              | 🔧     |
| Dialogue system stability                      | 🔧     |
| Audio/music playback in-game                   | 🔧     |
| Save/load during gameplay                      | 🔧     |
| Progression correctness (day/season/year)      | 📋     |
| Crash-free first full in-game day              | 📋     |
| End-of-year / credits sequence                 | 📋     |

---

## 2. PC Quality-of-Life

Goals: make the game comfortable to play on PC without changing its character.

| Item                                           | Status |
|------------------------------------------------|--------|
| Keyboard input (default mapping)               | ✅     |
| Gamepad input (first detected device)          | ✅     |
| Analog stick dead zone (drift prevention)      | ✅     |
| Fullscreen toggle (F11)                        | ✅     |
| ROM path as CLI argument                       | ✅     |
| Save file auto-created on first save           | ✅     |
| Windowed mode                                  | ✅     |
| Configurable keyboard bindings                 | 📋     |
| Configurable gamepad bindings                  | 📋     |
| Multiple gamepad support                       | 📋     |
| Resolution / aspect ratio options              | 📋     |
| Config file (JSON/TOML) for user settings      | 📋     |
| Per-user save directory (XDG / AppData)        | 📋     |
| In-game pause / quit shortcut                  | 📋     |
| Faster text scroll option (optional)           | 💡     |
| Auto-save warning on window close              | 💡     |

---

## 3. Faithful Enhancements

Goals: improvements that feel at home in the original game and do not change
its pacing, progression, or tone.

| Item                                           | Status |
|------------------------------------------------|--------|
| Original game bug fixes (graphics rotation)   | ✅     |
| Native save system (no memory card friction)   | ✅     |
| Original aspect ratio preserved by default     | ✅     |
| Fix original graphical glitches where safe     | 📋     |
| Optional HUD improvements (legibility)         | 💡     |
| Optional higher-resolution font rendering      | 💡     |
| Optional framerate smoothing (non-invasive)    | 💡     |

The bar for this section is high: any enhancement must feel invisible to a
player who doesn't know it's there.

---

## 4. User Testing and Releases

Goals: make it easy for testers to find, report, and reproduce issues.

| Item                                           | Status |
|------------------------------------------------|--------|
| README with clear build instructions           | ✅     |
| Troubleshooting guide                          | ✅     |
| Controls documentation                         | ✅     |
| Save file documentation                        | ✅     |
| ROADMAP.md                                     | ✅     |
| CONTRIBUTING.md                                | ✅     |
| GitHub issue templates (bug report, feature)   | ✅     |
| First test build (title screen milestone)      | 🔧     |
| Gameplay regression test checklist             | 📋     |
| Known issues list (maintained in-repo)         | 📋     |
| GitHub Releases with pre-built binaries        | 📋     |
| Changelog                                      | 📋     |
| Platform validation (Linux, Windows/WSL2)      | 📋     |

---

## 5. Developer Experience

Goals: make it easy for contributors to get started and stay productive.

| Item                                           | Status |
|------------------------------------------------|--------|
| Single `make recomp` command for full build    | ✅     |
| N64Recomp tool build automated via Makefile    | ✅     |
| CMake build for PC port                        | ✅     |
| `recomp/README.md` with port internals         | ✅     |
| Git submodule setup documented                 | ✅     |
| CMakePresets.json (debug + release)            | ✅     |
| Decomp.me preset for HM64                      | ✅     |
| Verbose build mode (`VERBOSE=1`)               | ✅     |
| Debug binary build documented                  | ✅     |
| Better warning/error messages (missing files)  | ✅     |
| Cleaner patch file organization                | 📋     |
| Developer build guide (separate doc)           | 📋     |
| RDP/RSP patch test harness                     | 📋     |

---

## Milestone Summary

| Milestone                       | Target state                                    |
|---------------------------------|-------------------------------------------------|
| **M1 – Title Screen** (current) | Game boots, title renders, input works, saves   |
| **M2 – First Day**              | Start new game → play a full first in-game day  |
| **M3 – Stable Season**          | Play through Spring Year 1 without crashes      |
| **M4 – Full Year**              | Reach end of Year 1, credits roll               |
| **M5 – Polish**                 | QoL settings, bindings, resolution, releases    |

We are currently working toward **M2**.
