#!/usr/bin/env bash
set -euo pipefail

# Recomp dependencies are VENDORED directly into this repository (under
# tools/n64recomp, recomp/lib/N64ModernRuntime, and recomp/lib/RT64), so no
# network access or git submodules are required. Older snapshots fetched these
# via `git clone`; if this script is run on such a checkout it will also restore
# the nested submodules vendored under recomp/output-funcs-style clones.
#
# This script only VALIDATES that the vendored dependency sources are present
# and complete. It intentionally performs no cloning.

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
        fail "Missing vendored dependency file: ${required_file#$ROOT_DIR/}"
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    exit 1
fi

log "All vendored recomp dependencies are ready."