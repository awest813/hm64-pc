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
# Directory enumeration supplies file metadata in batches. Separate stat calls
# for every file are particularly expensive on Windows-mounted WSL checkouts.
python3 - "$ROOT_DIR" "$MANIFEST" <<'PY'
from collections import defaultdict
from pathlib import Path
import os
import sys

root, manifest = map(Path, sys.argv[1:])
groups = defaultdict(list)
for relative_path in manifest.read_text().splitlines():
    path = Path(relative_path)
    groups[path.parent].append(path.name)

missing = []
count = 0
for parent, names in groups.items():
    try:
        with os.scandir(root / parent) as entries:
            files = {entry.name for entry in entries if entry.is_file()}
    except OSError:
        files = set()
    for name in names:
        if name in files:
            count += 1
        else:
            missing.append(parent / name)

for path in missing:
    print(f'[recomp-bootstrap] MISSING {path}', file=sys.stderr)
if missing:
    sys.exit('[recomp-bootstrap] error: One or more vendored dependency files are missing. '
             'Re-clone or re-download this repository.')
print(f'[recomp-bootstrap] All {count} vendored dependency files are present '
      '(including nested former submodules).')
PY
