#!/usr/bin/env python3
"""Bundle the linked game's data and map linked asset offsets to the source ROM."""
import argparse
from pathlib import Path
import re
import struct

import yaml


def generate(elf_path, config_path, output):
    elf = elf_path.read_bytes()
    if elf[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected a big-endian ELF32 game executable")
    shoff = struct.unpack_from(">I", elf, 32)[0]
    shentsize, shnum = struct.unpack_from(">HH", elf, 46)
    sections = [struct.unpack_from(">10I", elf, shoff + i * shentsize)
                for i in range(shnum)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = elf[strings[4]:strings[4] + strings[5]]
        for off in range(section[4], section[4] + section[5], section[9]):
            name, value = struct.unpack_from(">II", elf, off)
            if name:
                symbols[names[name:names.index(b"\0", name)].decode()] = value

    start = symbols["_codeSegmentDataStart"]
    end = symbols["_codeSegmentOvlEnd"]
    data_section = next(s for s in sections if s[3] <= start < s[3] + s[5]
                        and s[1] == 1)
    offset = data_section[4] + start - data_section[3]
    data = elf[offset:offset + end - start]
    if len(data) != end - start:
        raise ValueError("Truncated ELF data")

    segments = yaml.safe_load(config_path.read_text())["segments"]
    mappings = []
    for i, segment in enumerate(segments[:-1]):
        if not isinstance(segment, dict) or "name" not in segment:
            continue
        name = segment["name"]
        lo = symbols.get(f"_{name}SegmentRomStart")
        hi = symbols.get(f"_{name}SegmentRomEnd")
        if lo is None or hi is None or hi <= lo or lo < symbols["_codeSegmentRomEnd"]:
            continue
        following = segments[i + 1]
        original_end = following["start"] if isinstance(following, dict) else following[0]
        if hi - lo > original_end - segment["start"]:
            raise ValueError(f"Linked asset {name} exceeds its original ROM segment")
        mappings.append((lo, hi, segment["start"]))

    output.mkdir(parents=True, exist_ok=True)
    with (output / "hm64_runtime_data.h").open("w") as header:
        header.write("// Generated from the same ELF as the recompiled functions.\n#pragma once\n#include <cstdint>\nnamespace hm64::build {\n")
        for name, value in sorted(symbols.items()):
            if re.fullmatch(r"[A-Za-z_]\w*", name):
                header.write(f"inline constexpr uint32_t sym_{name} = 0x{value:08X}u;\n")
        header.write(f"inline constexpr uint32_t data_address = 0x{start:08X}u;\n")
        header.write("extern const unsigned char data[];\n")
        header.write(f"inline constexpr unsigned data_size = {len(data)};\n")
        header.write("struct RomSegment { uint32_t begin, end, original; };\ninline constexpr RomSegment rom_segments[] = {\n")
        for lo, hi, original in sorted(mappings):
            header.write(f"{{0x{lo:X}u, 0x{hi:X}u, 0x{original:X}u}},\n")
        header.write("};\n}\n")
    with (output / "hm64_runtime_data.cpp").open("w") as source:
        source.write('#include "hm64_runtime_data.h"\nconst unsigned char hm64::build::data[] = {\n')
        for i in range(0, len(data), 24):
            source.write(",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
        source.write("};\n")
    print(f"Bundled {len(data)} data bytes and {len(mappings)} ROM segment mappings")

    # GCC emits local symbols such as foo.part.0. They are valid ELF symbols,
    # but cannot be emitted verbatim as C identifiers by this recompiler.
    funcs = output / "funcs"
    declarations = funcs / "funcs.h"
    if declarations.exists():
        names = re.findall(r"void ([\w.]+)\(", declarations.read_text())
        renames = {name: name.replace(".", "_") for name in names if "." in name}
        if len(set(renames.values())) != len(renames) or set(renames.values()) & set(names):
            raise ValueError("Generated C symbol normalization would collide")
        for path in funcs.iterdir():
            if path.suffix not in (".c", ".h", ".inl"):
                continue
            source = path.read_text()
            for old, new in renames.items():
                source = re.sub(r"(?<![\w.])" + re.escape(old) + r"(?![\w.])", new, source)
            if path == declarations:
                for name in ("osPfsNumFiles_recomp", "osPfsRepairId_recomp"):
                    if f"void {name}(" not in source:
                        source += f"\nvoid {name}(uint8_t* rdram, recomp_context* ctx);\n"
            if source != path.read_text():
                path.write_text(source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=Path("hm64.elf"))
    parser.add_argument("--config", type=Path, default=Path("config/us/splat.us.yaml"))
    parser.add_argument("--output", type=Path, default=Path("recomp/output"))
    args = parser.parse_args()
    generate(args.elf, args.config, args.output)
