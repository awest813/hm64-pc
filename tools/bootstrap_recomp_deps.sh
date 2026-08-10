#!/usr/bin/env bash
set -euo pipefail

# Recomp dependencies are VENDORED directly into this repository under
# tools/n64recomp, recomp/lib/N64ModernRuntime, and recomp/lib/RT64, so a fresh
# clone or ZIP download needs no submodules and no network fetch. This script
# validates that those vendored sources are present and complete. It performs no
# cloning or downloading.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

N64RECOMP_DIR="$ROOT_DIR/tools/n64recomp"
RUNTIME_DIR="$ROOT_DIR/recomp/lib/N64ModernRuntime"
RT64_DIR="$ROOT_DIR/recomp/lib/RT64"

log() {
    printf '[recomp-bootstrap] %s\n' "$1"
}

fail() {
    printf '[recomp-bootstrap] error: %s\n' "$1" >&2
    exit 1
}

required_files=(
    "$N64RECOMP_DIR/CMakeLists.txt"
    "$N64RECOMP_DIR/src/main.cpp"
    "$N64RECOMP_DIR/lib/fmt/CMakeLists.txt"
    "$N64RECOMP_DIR/lib/tomlplusplus/CMakeLists.txt"
    "$RUNTIME_DIR/CMakeLists.txt"
    "$RUNTIME_DIR/N64Recomp/CMakeLists.txt"
    "$RUNTIME_DIR/thirdparty/miniz/CMakeLists.txt"
    "$RT64_DIR/CMakeLists.txt"
    "$RT64_DIR/src/hle/rt64_interpreter.cpp"
)

missing=0
for required_file in "${required_files[@]}"; do
    if [[ -f "$required_file" ]]; then
        log "OK  ${required_file#$ROOT_DIR/}"
    else
        printf '[recomp-bootstrap] MISSING %s\n' "${required_file#$ROOT_DIR/}" >&2
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    fail "One or more vendored dependency files are missing. Re-clone or re-download this repository."
fi

log "All vendored recomp dependencies are ready."
