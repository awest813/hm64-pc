# HM64 PC Fork — Public Testing Guide

This guide is for first-time testers validating the PC fork.

## 1) Environment setup

```sh
tools/setup.sh --install-system-deps
make recomp-deps
cp /path/to/baserom.us.z64 .
make doctor
```

If doctor reports failures, follow its suggested fix path before continuing.

## 2) Build and run

```sh
make pc
./recomp/build/hm64_pc
```

## 3) Basic smoke test checklist

Please verify the following in order:

1. App launches and shows a window
2. Title/menu flow appears
3. Keyboard controls respond:
   - `Enter` on menu
   - movement inputs
4. Gamepad controls respond (if connected), including:
   - left stick movement
   - A/B/Start
   - right stick C-button equivalents
5. Fullscreen toggles with `F11`
6. Quit works with `Escape`
7. Save file behavior:
   - progress save creates `hm64.sav`
   - save persists after relaunch

## 4) Report quality checklist

When filing a bug, include:

- exact commit (`git rev-parse --short HEAD`)
- platform and distro/WSL info
- exact commands used to build/run
- expected vs actual behavior
- reproduction steps
- terminal output (especially `make doctor` and runtime logs)
- screenshot/video if visual/input issue

Use the repository issue templates in `.github/ISSUE_TEMPLATE/`.

## 5) Faithfulness testing focus

Because this fork prioritizes the original HM64 feel, report anything that seems to:

- alter pacing/progression unintentionally
- change menu/input behavior in a way that feels unlike original intent
- introduce QoL behavior that is too invasive for default play

Those regressions are high priority even if they are not hard crashes.
