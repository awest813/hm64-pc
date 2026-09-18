#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIP_ROM=0
ROM_PATH="$ROOT_DIR/baserom.us.z64"
ROM_SHA1_EXPECTED="90631460f1876a14849df0541d534012b410a34c"

for arg in "$@"; do
    case "$arg" in
        --skip-rom) SKIP_ROM=1 ;;
        *) ROM_PATH="$arg" ;;
    esac
done

status=0

pass() {
    printf '[doctor] ✅ %s\n' "$1"
}

warn() {
    printf '[doctor] ⚠️  %s\n' "$1"
}

fail() {
    printf '[doctor] ❌ %s\n' "$1"
    status=1
}

check_cmd() {
    local cmd="$1"
    local hint="$2"
    if command -v "$cmd" >/dev/null 2>&1; then
        pass "Found command: $cmd"
    else
        fail "Missing command: $cmd ($hint)"
    fi
}

check_python_module() {
    local module="$1"
    local hint="$2"
    if python3 -c "import $module" >/dev/null 2>&1; then
        pass "Python module available: $module"
    else
        fail "Missing Python module: $module ($hint)"
    fi
}

echo "[doctor] Running HM64 build preflight checks..."
echo "[doctor] Repo root: $ROOT_DIR"

check_cmd python3 "install python3"
check_cmd gcc "install build-essential"
check_cmd c++ "install g++ (or build-essential)"
check_cmd cmake "install cmake"
check_cmd make "install build-essential"
check_cmd mips-linux-gnu-as "install binutils-mips-linux-gnu"
check_cmd mips-linux-gnu-gcc "install gcc-mips-linux-gnu (required for PC builds)"
check_cmd mips-linux-gnu-ld "install binutils-mips-linux-gnu"
check_cmd mips-linux-gnu-objcopy "install binutils-mips-linux-gnu"
check_cmd git "install git"
check_cmd wget "install wget"
check_cmd tar "install tar"

if python3 -m pip --version >/dev/null 2>&1; then
    pass "python3 -m pip is available"
else
    fail "python3 -m pip is unavailable (install python3-pip)"
fi

if [[ -x "$ROOT_DIR/tools/gcc-2.7.2/gcc" ]]; then
    pass "Found local GCC 2.7.2 toolchain (tools/gcc-2.7.2/gcc, vendored)"
else
    fail "Missing local GCC 2.7.2 toolchain. Run: tools/setup.sh"
fi

if [[ -x "$ROOT_DIR/tools/gcc-2.7.2/as" ]]; then
    pass "Found local mips binutils (tools/gcc-2.7.2/as, vendored)"
else
    fail "Missing local mips binutils. Run: tools/setup.sh"
fi

if [[ -x "$ROOT_DIR/tools/build/mkldscript" ]]; then
    pass "Found mkldscript helper (tools/build/mkldscript)"
else
    fail "Missing mkldscript helper. Run: tools/setup.sh"
fi

check_python_module splat "run: tools/setup.sh"
check_python_module PIL "run: tools/setup.sh"
check_python_module numpy "run: tools/setup.sh"

if bash "$ROOT_DIR/tools/bootstrap_recomp_deps.sh"; then
    pass "Vendored dependencies (including nested sources) are complete"
else
    fail "Vendored dependency verification failed"
fi

if command -v c++ >/dev/null 2>&1; then
    tmp_cpp="$(mktemp --suffix=.cpp)"
    tmp_bin="$(mktemp)"
    printf 'int main(){return 0;}\n' > "$tmp_cpp"
    if c++ "$tmp_cpp" -o "$tmp_bin" >/dev/null 2>&1; then
        pass "C++ linker sanity check passed"
    else
        fail "C++ linker sanity check failed (install g++/libstdc++ development packages)"
    fi
    rm -f "$tmp_cpp" "$tmp_bin"
fi

if [[ "$SKIP_ROM" -eq 1 ]]; then
    warn "ROM checks skipped (--skip-rom); this validates toolchain and deps only."
else
    if [[ -f "$ROM_PATH" ]]; then
        pass "Found ROM: $ROM_PATH"
        if command -v sha1sum >/dev/null 2>&1; then
            rom_sha1="$(sha1sum "$ROM_PATH" | awk '{print $1}')"
            if [[ "$rom_sha1" == "$ROM_SHA1_EXPECTED" ]]; then
                pass "ROM SHA-1 matches expected US version"
            else
                fail "ROM SHA-1 mismatch (got $rom_sha1, expected $ROM_SHA1_EXPECTED)"
            fi
        else
            warn "sha1sum not available; skipped ROM hash validation."
        fi
    else
        fail "Missing ROM at $ROM_PATH (expected baserom.us.z64)"
    fi
fi

if command -v pkg-config >/dev/null 2>&1; then
    for package in dbus-1; do
        if pkg-config --exists "$package"; then
            pass "Development package found: $package"
        else
            fail "Missing $package development files; run tools/setup.sh --install-system-deps"
        fi
    done
    if pkg-config --exists sdl2; then
        pass "SDL2 development package found via pkg-config"
    else
        fail "SDL2 development package not found (install libsdl2-dev)"
    fi
else
    fail "pkg-config not found; install pkg-config."
fi

if [[ "$status" -eq 0 ]]; then
    echo "[doctor] All checks passed."
    exit 0
fi

echo "[doctor] One or more checks failed."
echo "[doctor] Suggested fix path:"
echo "  1) tools/setup.sh --install-system-deps"
echo "  2) make recomp-deps"
echo "  3) place baserom.us.z64 in repo root"
echo "  4) run: make doctor"
exit 1
