---
name: Bug report
about: Report a crash, visual glitch, wrong behavior, or build failure
title: "[BUG] "
labels: bug
assignees: ''
---

## Describe the bug

A clear and concise description of what went wrong.

## Steps to reproduce

1. ...
2. ...
3. ...

## Expected behavior

What you expected to happen.

## Actual behavior

What actually happened. Include any error messages, crash output, or screenshots.

## Build information

- **OS:** (e.g. Ubuntu 22.04, Windows 11 + WSL2)
- **Compiler:** (e.g. clang 15, gcc 12)
- **CMake version:** (run `cmake --version`)
- **SDL2 version:** (run `sdl2-config --version` if available)
- **Branch / commit:** (run `git rev-parse --short HEAD`)

## How you built the project

```sh
# paste the exact commands you ran
```

## Additional context

Any other context, logs, or screenshots that might help diagnose the issue.

## Checklist

- [ ] I have run `make recomp-deps`
- [ ] I have run `make doctor`
- [ ] I have run `make recomp-generate` before `make recomp-build`
- [ ] My ROM is `baserom.us.z64` (big-endian/Z64 format)
- [ ] I have checked the [Troubleshooting section](../../README.md#troubleshooting) of the README
