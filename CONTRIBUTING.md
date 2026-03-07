# Contributing to HM64 PC Port

Thank you for your interest in contributing! This guide covers how to get
involved, what kinds of contributions are welcome, and the design principles
that shape this fork.

---

## Philosophy

> *Bring Harvest Moon 64 to PC with respectful quality-of-life improvements,
> while staying true to the spirit, pacing, atmosphere, and heart of the
> original game.*

Every change should feel like it belongs in HM64. If a feature would make the
game feel like a different game, it does not belong in this fork.

**What fits:**
- Bug fixes that make the game more correct or stable
- PC conveniences (keyboard, gamepad, save paths, window management)
- Non-invasive quality-of-life improvements that feel invisible
- Decomp correctness improvements (better labels, struct names, macro values)

**What does not fit:**
- Mechanical redesigns or rebalancing
- Pacing changes (faster time, skip-able content)
- UI overhauls that change the feel of the original menus
- Features that clash with the original game's tone or identity

When in doubt: *would a player who doesn't know the fork exists notice this
change?* If yes, it needs to be optional or reconsidered.

---

## Getting Started

1. Fork the repo and clone it with submodules:
   ```sh
   git clone --recursive https://github.com/awest813/hm64-pc.git
   ```

2. Follow the build instructions in [README.md](README.md).

3. Create a branch for your work:
   ```sh
   git checkout -b my-feature-or-fix
   ```

4. Make your changes and test them.

5. Open a pull request against `main` (or the appropriate feature branch).

---

## High-Priority Contribution Areas

### Testing (right now — most valuable)

- Build the project and report anything that breaks
- Test gameplay past the title screen
- Report crashes, visual glitches, audio issues, or wrong behavior
- Use the [bug report template](.github/ISSUE_TEMPLATE/bug_report.md)

### Decomp quality

- Clean up fake/forced matches (search `FIXME` in the source)
- Research function, struct, and variable purposes and improve labels
- Add macro values (e.g., player action constants in `player.h`)
- Improve labeling in bytecode files (`src/bytecode/`)
- JP version matching (only scaffolding exists so far)

### PC port patches

- Fix bugs in `recomp/patches/` files
- Improve NuSystem stubs in `recomp/patches/nusys_patches.cpp`
- Improve audio reliability in `recomp/platform/audio.cpp`
- Improve gamepad support in `recomp/platform/input.cpp`

### Documentation

- Improve troubleshooting guidance based on real issues encountered
- Document struct fields and game systems as you research them
- Improve the asset extraction tool documentation

---

## Code Style

- Follow the style of the surrounding code
- Prefer descriptive names over abbreviations
- Keep PC-specific patches in `recomp/patches/` — do not modify decompiled
  source in `src/` for PC-only behavior
- Document non-obvious patches with a comment explaining *why*, not just *what*

---

## Commit Messages

Use short, clear commit messages:
```
Fix analog stick drift in title screen
Stub nuGfxTaskStart to prevent hang on boot
Improve keyboard mapping documentation
```

---

## Reporting Issues

Please use the GitHub issue templates:
- **Bug report** — crashes, visual glitches, wrong behavior
- **Feature request** — new ideas or improvements

Include your platform, build steps, and steps to reproduce the issue.

---

## Decomp.me

For N64 function matching work, use the **Harvest Moon 64** compiler preset
on [decomp.me](https://decomp.me) when creating a new scratch.

---

## Questions?

Open an issue or start a discussion in the repository. We are happy to help
new contributors get oriented.
