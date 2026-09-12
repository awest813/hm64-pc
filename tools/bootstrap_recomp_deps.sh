#!/usr/bin/env bash
set -euo pipefail

# Recomp dependencies are VENDORED directly into this repository under
# tools/n64recomp, recomp/lib/N64ModernRuntime, and recomp/lib/RT64, so a fresh
# clone or ZIP download needs no submodules and no network fetch. This script
# validates that those vendored sources are present and complete. It performs no
# cloning or downloading.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

log() {
    printf '[recomp-bootstrap] %s\n' "$1"
}

fail() {
    printf '[recomp-bootstrap] error: %s\n' "$1" >&2
    exit 1
}

MANIFEST="$ROOT_DIR/tools/recomp-deps.manifest"
[[ -s "$MANIFEST" ]] || fail "Missing dependency manifest: $MANIFEST"

log "Checking vendored dependency files (including nested sources)..."
missing=0
count=0
mapfile -t required_files < "$MANIFEST"
for relative_path in "${required_files[@]}"; do
    required_file="$ROOT_DIR/$relative_path"
    if [[ -f "$required_file" ]]; then
        count=$((count + 1))
    else
        printf '[recomp-bootstrap] MISSING %s\n' "${required_file#$ROOT_DIR/}" >&2
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    fail "One or more vendored dependency files are missing. Re-clone or re-download this repository."
fi

log "All $count vendored dependency files are present (including nested former submodules)."
