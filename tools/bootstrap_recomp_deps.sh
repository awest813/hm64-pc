#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

N64RECOMP_DIR="$ROOT_DIR/tools/n64recomp"
RUNTIME_DIR="$ROOT_DIR/recomp/lib/N64ModernRuntime"

N64RECOMP_URL="https://github.com/N64Recomp/N64Recomp.git"
RUNTIME_URL="https://github.com/N64Recomp/N64ModernRuntime.git"

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

log "All recomp dependencies are ready."
