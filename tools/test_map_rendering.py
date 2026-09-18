#!/usr/bin/env python3
"""Exercise the generated MIPS map renderer with synthetic data, without a ROM.

Run after `make MODERN_GCC=1 recomp-generate`: python3 tools/test_map_rendering.py
The opening cutscene depends on rendering preserving the caller's registers;
a stack overwrite here corrupts the visibility grid and stalls NPC animations.
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    functions = {}
    for path in (ROOT / "recomp/output/funcs").glob("*.c"):
        for match in re.finditer(
            r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC|\Z)",
            path.read_text(), re.S,
        ):
            functions[match[1]] = match[0]
    needed = set()
    pending = ["appendTileToDL", "prepareTileTextures",
               "initializeMessageBoxInterpolator", "stepMessageBoxInterpolator",
               "updateScrollDownAnimation"]
    while pending:
        name = pending.pop()
        if name in needed:
            continue
        if name not in functions:
            raise SystemExit(f"Missing generated function {name}; run recomp-generate first")
        needed.add(name)
        pending.extend(re.findall(r"^    (\w+)\(rdram, ctx\);", functions[name], re.M))

    source = '#include "recomp.h"\n#include <cassert>\n#include <cstdio>\n#include <vector>\n'
    source += ''.join(f'void {name}(uint8_t*, recomp_context*);\n' for name in sorted(needed))
    source += ''.join(functions[name] for name in sorted(needed))
    source += (ROOT / "recomp/tests/map_rendering_test.cpp").read_text()
    with tempfile.TemporaryDirectory(prefix="hm64-map-test-") as directory:
        cpp = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        cpp.write_text(source)
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++20", "-O0", "-UNDEBUG",
            "-I", str(ROOT / "recomp/lib/N64ModernRuntime/N64Recomp/include"),
            str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
