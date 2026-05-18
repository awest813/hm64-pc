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
  - installs/updates Python dependencies for the current user
  - downloads the GCC 2.7.2 + binutils 2.6 toolchain into tools/gcc-2.7.2
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
command -v wget >/dev/null 2>&1 || fail "wget not found"
command -v tar >/dev/null 2>&1 || fail "tar not found"
command -v gcc >/dev/null 2>&1 || fail "gcc not found"

if [[ "$INSTALL_SYSTEM_DEPS" -eq 1 ]]; then
    command -v sudo >/dev/null 2>&1 || fail "sudo is required for --install-system-deps"
    run "Installing Ubuntu/WSL system packages" \
        sudo apt-get update
    run "Installing toolchain/system dependencies" \
        sudo apt-get install -y \
            build-essential cmake ninja-build clang \
            python3 python3-pip \
            binutils-mips-linux-gnu gcc-mips-linux-gnu \
            libsdl2-dev pkg-config
else
    warn "Skipping apt package install. Use --install-system-deps on Ubuntu/WSL."
fi

mkdir -p "$KMC_DIR"

if [[ ! -x "$KMC_DIR/gcc" ]]; then
    run "Downloading GCC 2.7.2 toolchain" \
        bash -lc "wget -c \"https://github.com/decompals/mips-gcc-2.7.2/releases/download/main/gcc-2.7.2-linux.tar.gz\" -O - | tar -xz -C \"$KMC_DIR\""
else
    log "GCC 2.7.2 already present at tools/gcc-2.7.2/gcc"
fi

if [[ ! -x "$KMC_DIR/as" ]]; then
    run "Downloading mips binutils 2.6" \
        bash -lc "wget -c \"https://github.com/decompals/mips-binutils-2.6/releases/latest/download/binutils-2.6-linux.tar.gz\" -O - | tar -xz -C \"$KMC_DIR\""
else
    log "mips binutils already present at tools/gcc-2.7.2/as"
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
run "Installing Python dependencies (user scope)" \
    python3 -m pip install --user --break-system-packages -U -r "$REQ_FILE"

log "Done. Next recommended command: make doctor"