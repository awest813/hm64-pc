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

The startup log will print the absolute path to `hm64.sav` and a control
reference.  Keep the terminal open so you can capture any error output.

## 3) Basic smoke test checklist

Please verify the following in order:

1. App launches and shows a window
2. Title/menu flow appears
3. Keyboard controls respond:
   - `Enter` on menu
   - movement inputs (WASD for analog, arrow keys for D-pad)
4. Gamepad controls respond (if connected), including:
   - left stick movement
   - A/B/Start
   - **Z button** on either trigger (left or right)
   - right stick C-button equivalents
5. Fullscreen toggles with `F11`
6. Quit works with `Escape`
7. Save file behavior:
   - progress save creates `hm64.sav` at the path printed on startup
   - save persists after relaunch

## 4) Gameplay regression checklist

Run through these scenarios and note any deviations from the original N64
behavior.  You do not need to complete every scenario in one session; file
separate bug reports for each issue found.

### 4a) Title screen and new-game flow

- [ ] HM64 logo renders correctly; no missing or corrupted sprites
- [ ] "Press Start" / title animation plays without glitches
- [ ] Pressing Start advances to the name-entry / new-game screen
- [ ] On-screen text is legible; no character rendering issues

### 4b) Opening cutscene and tutorial day

- [ ] Opening cutscene plays; audio and visuals stay in sync
- [ ] Player character spawns on the farm
- [ ] Walking in all four directions works; diagonal movement feels natural
- [ ] Transition between map areas (farm → town, etc.) loads without a hang
- [ ] NPC dialogue triggers on interaction; text box displays correctly
- [ ] Tool usage (hoe, watering can) animates and affects the game world

### 4c) In-game rendering

- [ ] Farm buildings and terrain render without Z-fighting or missing geometry
- [ ] Weather effects (rain, sun) display correctly
- [ ] Day/night lighting transitions are smooth
- [ ] HUD elements (stamina, money, date) display correctly and update
- [ ] No persistent screen-tearing or framerate hitches during normal play
- [ ] Inventory / shop screens open and display item icons correctly

### 4d) Audio

- [ ] Background music plays on the title screen and in-game
- [ ] Music transitions between areas without a noticeable gap or stutter
- [ ] Sound effects (footsteps, tool use, NPC voice clips) play at the right
     time
- [ ] No audio popping, crackling, or looping artefacts after 10+ minutes
- [ ] Audio stays approximately in sync with on-screen action throughout a
     full in-game day

### 4e) Save / load

- [ ] Sleeping (end-of-day save) writes to `hm64.sav`
- [ ] After quitting and restarting, the saved game loads correctly
- [ ] Loaded game reflects the correct in-game date, money, and inventory
- [ ] Save slot selection screen (if shown) works without errors
- [ ] No data corruption after multiple sequential save/load cycles

### 4f) Extended session

- [ ] Play for ≥ 30 minutes without a crash or hang
- [ ] Audio latency does not noticeably increase over time
- [ ] Memory usage (OS task manager) does not grow unboundedly
- [ ] Frame rate remains stable across multiple screen transitions

## 5) Report quality checklist

When filing a bug, include:

- exact commit (`git rev-parse --short HEAD`)
- platform and distro/WSL info
- exact commands used to build/run
- expected vs actual behavior
- reproduction steps
- terminal output (especially `make doctor` and runtime logs)
- screenshot/video if visual/input issue

Use the repository issue templates in `.github/ISSUE_TEMPLATE/`.

## 6) Faithfulness testing focus

Because this fork prioritises the original HM64 feel, report anything that
seems to:

- alter pacing/progression unintentionally
- change menu/input behavior in a way that feels unlike original intent
- introduce QoL behavior that is too invasive for default play

Those regressions are high priority even if they are not hard crashes.

## 7) Known limitations (not bugs)

See `KNOWN_ISSUES.md` for a full list.  Do **not** file bugs for items already
listed there unless you have new information (a reproduction path, a patch, or
evidence that the issue is worse than documented).
