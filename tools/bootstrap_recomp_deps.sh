#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

N64RECOMP_REL="tools/n64recomp"
RUNTIME_REL="recomp/lib/N64ModernRuntime"
RT64_REL="recomp/lib/RT64"

N64RECOMP_DIR="$ROOT_DIR/$N64RECOMP_REL"
RUNTIME_DIR="$ROOT_DIR/$RUNTIME_REL"
RT64_DIR="$ROOT_DIR/$RT64_REL"

N64RECOMP_URL="https://github.com/N64Recomp/N64Recomp.git"
RUNTIME_URL="https://github.com/N64Recomp/N64ModernRuntime.git"
RT64_URL="https://github.com/rt64/rt64.git"

log() {
    printf '[recomp-bootstrap] %s\n' "$1"
}

warn() {
    printf '[recomp-bootstrap] warning: %s\n' "$1" >&2
}

fail() {
    printf '[recomp-bootstrap] error: %s\n' "$1" >&2
    exit 1
}

ensure_repo() {
    local target="$1"
    local cmake_file="$2"
    local repo_url="$3"

    if [[ -f "$target/$cmake_file" ]]; then
        log "Found $target/$cmake_file"
        return 0
    fi

    # Try submodule init first (works when gitlinks are present).
    if git -C "$ROOT_DIR" submodule update --init --recursive -- "$target" >/dev/null 2>&1; then
        if [[ -f "$target/$cmake_file" ]]; then
            log "Initialized dependency via git submodule: $target"
            return 0
        fi
    else
        warn "Submodule init unavailable for $target in this branch snapshot; trying clone fallback."
    fi

    if [[ -d "$target/.git" ]]; then
        log "Dependency exists as a git checkout: $target"
        if [[ -f "$target/$cmake_file" ]]; then
            return 0
        fi
        fail "$target exists but $cmake_file is missing. Please remove and retry."
    fi

    if [[ -d "$target" ]] && [[ -n "$(ls -A "$target" 2>/dev/null)" ]]; then
        fail "$target exists and is non-empty, but is not a valid checkout. Please remove it and retry."
    fi

    mkdir -p "$(dirname "$target")"
    log "Cloning dependency: $repo_url -> $target"
    git clone --depth 1 "$repo_url" "$target"

    [[ -f "$target/$cmake_file" ]] || fail "Clone succeeded but $cmake_file missing in $target"
}

ensure_repo "$N64RECOMP_DIR" "CMakeLists.txt" "$N64RECOMP_URL"
ensure_repo "$RUNTIME_DIR" "CMakeLists.txt" "$RUNTIME_URL"
ensure_repo "$RT64_DIR" "CMakeLists.txt" "$RT64_URL"

log "Initializing nested dependency submodules..."
if ! git -C "$ROOT_DIR" submodule update --init --recursive -- "$N64RECOMP_REL" "$RUNTIME_REL" "$RT64_REL"; then
    warn "Some dependencies are not registered as submodules in this checkout; validating existing checkouts instead."
fi

if [[ -e "$N64RECOMP_DIR/.git" ]]; then
    git -C "$N64RECOMP_DIR" submodule update --init --recursive
fi

if [[ -e "$RUNTIME_DIR/.git" ]]; then
    git -C "$RUNTIME_DIR" submodule update --init --recursive
fi

if [[ -e "$RT64_DIR/.git" ]]; then
    git -C "$RT64_DIR" submodule update --init --recursive
fi

required_files=(
    "$N64RECOMP_DIR/CMakeLists.txt"
    "$N64RECOMP_DIR/lib/fmt/CMakeLists.txt"
    "$N64RECOMP_DIR/lib/tomlplusplus/CMakeLists.txt"
    "$RUNTIME_DIR/CMakeLists.txt"
    "$RUNTIME_DIR/N64Recomp/CMakeLists.txt"
    "$RUNTIME_DIR/thirdparty/miniz/CMakeLists.txt"
    "$RT64_DIR/CMakeLists.txt"
)

for required_file in "${required_files[@]}"; do
    [[ -f "$required_file" ]] || fail "Missing required dependency file: $required_file"
done

log "All recomp dependencies are ready."
