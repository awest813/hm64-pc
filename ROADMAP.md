# HM64 PC Fork — Roadmap

This roadmap reflects the current state of this fork and near-term priorities.
Statuses are evidence-based and intentionally conservative.

## Status key

- ✅ **Done**
- 🔧 **In Progress**
- 📋 **Planned**
- 💡 **Optional / Stretch**

---

## 1) Core Playability

Goal: stable and faithful core gameplay from boot through normal progression.

| Item | Status |
|---|---|
| US decompilation baseline integrated | ✅ |
| n64recomp pipeline integrated | ✅ |
| Native PC boot path established | ✅ |
| Title/menu rendering and input path | ✅ |
| SRAM-backed save file path (`hm64.sav`) | ✅ |
| Boot-to-gameplay stability validation | 🔧 |
| In-game rendering correctness | 🔧 |
| In-game audio reliability | 🔧 |
| Save/load correctness during long play sessions | 🔧 |
| Progression correctness (day/season/year) | 📋 |
| End-to-end year/credits validation | 📋 |

---

## 2) PC Quality-of-Life

Goal: practical PC usability without changing HM64’s identity.

| Item | Status |
|---|---|
| Keyboard default mapping | ✅ |
| Gamepad default mapping | ✅ |
| Gamepad hot-plug handling | ✅ |
| C-button equivalents on gamepad | ✅ |
| Fullscreen hotkey (`F11`) | ✅ |
| ROM CLI argument support | ✅ |
| Clear startup runtime info (ROM/save/hotkeys) | ✅ |
| User-configurable keybinds | 📋 |
| User-configurable gamepad mapping | 📋 |
| Per-user save directory (XDG/AppData) | 📋 |
| Runtime settings/config file | 📋 |
| Resolution/aspect options UI | 📋 |
| Accessibility-focused optional QoL toggles | 💡 |

---

## 3) Faithful Enhancements

Goal: improvements that preserve original pacing, tone, and progression.

| Item | Status |
|---|---|
| Native save persistence replacing hardware SRAM dependency | ✅ |
| Faithfulness-first contribution philosophy documented | ✅ |
| Non-invasive bug fixes that preserve original feel | 🔧 |
| Optional readability/UI polish that fits original style | 💡 |
| Optional convenience toggles guarded behind defaults | 💡 |

---

## 4) User Testing and Releases

Goal: make testing easy, reproducible, and high-signal.

| Item | Status |
|---|---|
| Tester-friendly README quick start | ✅ |
| Build preflight command (`make doctor`) | ✅ |
| Setup/bootstrap helper (`tools/setup.sh`) | ✅ |
| Runtime dependency bootstrap (`make recomp-deps`) | ✅ |
| Public testing guide (`TESTING.md`) | ✅ |
| Known issues tracking file (`KNOWN_ISSUES.md`) | ✅ |
| Issue templates (bug + feature) | ✅ |
| Structured gameplay regression checklist expansion | 🔧 |
| Tagged test-build release cadence | 📋 |
| Changelog process | 📋 |
| Multi-platform validation matrix | 📋 |

---

## 5) Developer Experience

Goal: reduce contributor setup friction and improve maintainability.

| Item | Status |
|---|---|
| `make help` onboarding target | ✅ |
| Actionable preflight diagnostics | ✅ |
| Idempotent local setup script | ✅ |
| Recomp dependency fallback when submodule gitlinks are absent | ✅ |
| Documented debug build flow | ✅ |
| Patch/runtime internals documentation | ✅ |
| CI workflow for automated validation | 📋 |
| Runtime patch test harnesses | 📋 |
| Additional architecture docs for major subsystems | 📋 |

---

## Milestones

| Milestone | Target |
|---|---|
| **M1 — Public Test Bootstrap** (current) | Clone → setup → doctor → build → run path is clear and reproducible |
| **M2 — First Stable Gameplay Session** | Reach and complete a full first in-game day with no blocker bugs |
| **M3 — Stable Season** | Play through a full season with reliable saves/audio/input |
| **M4 — Full-Year Validation** | End-of-year progression and credits validated |
| **M5 — Polish + Tester Releases** | Optional QoL, release packaging, broader platform confidence |
