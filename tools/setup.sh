#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KMC_DIR="$ROOT_DIR/tools/gcc-2.7.2"
MKLDSCRIPT_BIN="$ROOT_DIR/tools/build/mkldscript"
REQ_FILE="$ROOT_DIR/tools/requirements.txt"

INSTALL_SYSTEM_DEPS=0

usage() {
    cat <<'EOF'
Usage: tools/setup.sh [--install-system-deps]

Bootstraps HM64 build prerequisites in a repeatable, re-runnable way.

By default this script:
  - installs Python dependencies (user scope) from the vendored wheels in
    tools/python-wheels, with no network access (PyPI fallback if that fails)
  - verifies the vendored GCC 2.7.2 + binutils 2.6 toolchain in
    tools/gcc-2.7.2 (downloads it only if unexpectedly missing)
  - builds tools/build/mkldscript

Optional:
  --install-system-deps   Install apt packages (Ubuntu/WSL) via sudo.
EOF
}

log() {
    printf '[setup] %s\n' "$1"
}

warn() {
    printf '[setup] warning: %s\n' "$1" >&2
}

fail() {
    printf '[setup] error: %s\n' "$1" >&2
    exit 1
}

run() {
    log "$1"
    shift
    "$@"
}

for arg in "$@"; do
    case "$arg" in
        --install-system-deps) INSTALL_SYSTEM_DEPS=1 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown argument: $arg (use --help for usage)" ;;
    esac
done

command -v python3 >/dev/null 2>&1 || fail "python3 not found"
command -v tar >/dev/null 2>&1 || fail "tar not found"
command -v gcc >/dev/null 2>&1 || fail "gcc not found"
command -v wget >/dev/null 2>&1 || warn "wget not found (only needed for toolchain download fallback)"

if [[ "$INSTALL_SYSTEM_DEPS" -eq 1 ]]; then
    command -v sudo >/dev/null 2>&1 || fail "sudo is required for --install-system-deps"
    run "Installing Ubuntu/WSL system packages" \
        sudo apt-get update
    run "Installing toolchain/system dependencies" \
        sudo apt-get install -y \
            build-essential cmake ninja-build clang \
            python3 python3-pip python3-setuptools python3-wheel \
            libyaml-dev \
            binutils-mips-linux-gnu gcc-mips-linux-gnu \
            libsdl2-dev pkg-config
else
    warn "Skipping apt package install. Use --install-system-deps on Ubuntu/WSL."
fi

mkdir -p "$KMC_DIR"

if [[ ! -x "$KMC_DIR/gcc" ]]; then
    warn "Vendored GCC 2.7.2 missing from tools/gcc-2.7.2; downloading fallback"
    run "Downloading GCC 2.7.2 toolchain" \
        bash -lc "wget -c \"https://github.com/decompals/mips-gcc-2.7.2/releases/download/main/gcc-2.7.2-linux.tar.gz\" -O - | tar -xz -C \"$KMC_DIR\""
else
    log "GCC 2.7.2 found (vendored) at tools/gcc-2.7.2/gcc"
fi

if [[ ! -x "$KMC_DIR/as" ]]; then
    warn "Vendored mips binutils 2.6 missing from tools/gcc-2.7.2; downloading fallback"
    run "Downloading mips binutils 2.6" \
        bash -lc "wget -c \"https://github.com/decompals/mips-binutils-2.6/releases/latest/download/binutils-2.6-linux.tar.gz\" -O - | tar -xz -C \"$KMC_DIR\""
else
    log "mips binutils found (vendored) at tools/gcc-2.7.2/as"
fi

run "Marking local toolchain binaries executable" \
    chmod -R u+x "$KMC_DIR"

run "Building mkldscript helper" \
    gcc -o "$MKLDSCRIPT_BIN" \
        "$ROOT_DIR/tools/build/mkldscript.c" \
        "$ROOT_DIR/tools/build/spec.c" \
        "$ROOT_DIR/tools/build/util.c"
chmod +x "$MKLDSCRIPT_BIN"

python3 -m pip --version >/dev/null 2>&1 || fail "python3 -m pip is unavailable"
PIP_FLAGS=()
if python3 -c "import sysconfig" 2>/dev/null && \
   [[ -f "$(python3 -c 'import sysconfig; print(sysconfig.get_path("stdlib"))')/EXTERNALLY-MANAGED" ]]; then
    PIP_FLAGS+=(--break-system-packages)
fi
WHEELS_DIR="$ROOT_DIR/tools/python-wheels"
if [[ -d "$WHEELS_DIR" ]] && ls "$WHEELS_DIR"/*.whl >/dev/null 2>&1; then
    run "Installing Python dependencies from vendored wheels (offline)" \
        python3 -m pip install --user "${PIP_FLAGS[@]}" --no-index --find-links "$WHEELS_DIR" -r "$REQ_FILE"
else
    warn "Vendored wheels missing from tools/python-wheels; falling back to PyPI"
    run "Installing Python dependencies from PyPI" \
        python3 -m pip install --user "${PIP_FLAGS[@]}" -U -r "$REQ_FILE"
fi

log "Done. Next recommended command: make doctor"