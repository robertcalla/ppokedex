"""Regenerates source/core/tables.h from a PKHeX checkout.

Usage: python tools/gen_tables.py <path to PKHeX.Core>

Only needed if PKHeX adds new species offsets (e.g. a new generation).
"""
import pathlib
import re
import sys


def array(src: str, name: str) -> list[int]:
    m = re.search(name + r"\s*=>\s*\[(.*?)\];", src, re.S)
    if not m:
        sys.exit(f"array {name} not found")
    body = re.sub(r"//.*", "", m.group(1))
    out = []
    for tok in re.findall(r"-?0x[0-9A-Fa-f]+|-?\d+", body):
        neg = tok.startswith("-")
        t = tok.lstrip("-")
        v = int(t, 16) if t.lower().startswith("0x") else int(t, 10)
        out.append(-v if neg else v)
    return out


def fmt(values: list[int], width: int = 16) -> str:
    rows = [values[i:i + width] for i in range(0, len(values), width)]
    return ",\n".join("    " + ", ".join(str(v) for v in row) for row in rows)


def main() -> None:
    core = pathlib.Path(sys.argv[1])
    conv = next(core.rglob("SpeciesConverter.cs")).read_text(encoding="utf-8")
    swish = next(core.rglob("SwishCrypto.cs")).read_text(encoding="utf-8")

    g1 = array(conv, "Table1InternalToNational")
    g3 = array(conv, "Table3InternalToNational")
    g9 = array(conv, "Table9InternalToNational")
    pad = array(swish, "StaticXorpad")

    text = f"""// Generated from PKHeX (https://github.com/kwsch/PKHeX, GPLv3) by tools/gen_tables.py.
// Species index conversion tables and the SwishCrypto static xorpad.
#pragma once
#include <cstdint>

namespace pkx::tables {{

// Gen 1 internal index -> National Dex number.
inline constexpr uint8_t kGen1InternalToNational[{len(g1)}] = {{
{fmt(g1)}
}};

// Gen 3: species >= 277 are stored with an offset; national = raw + delta[raw - 277].
inline constexpr int kGen3FirstUnaligned = 277;
inline constexpr int8_t kGen3InternalToNationalDelta[{len(g3)}] = {{
{fmt(g3)}
}};

// Gen 9 (SV / Z-A): species >= 917 are stored with an offset.
inline constexpr int kGen9FirstUnaligned = 917;
inline constexpr int8_t kGen9InternalToNationalDelta[{len(g9)}] = {{
{fmt(g9)}
}};

// SwishCrypto static xorpad (0x7F bytes, extended to 0x80).
inline constexpr uint8_t kSwishXorpad[{len(pad)}] = {{
{fmt(pad)}
}};

}} // namespace pkx::tables
"""
    dest = pathlib.Path(__file__).resolve().parent.parent / "source" / "core" / "tables.h"
    dest.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {dest} (g1={len(g1)} g3={len(g3)} g9={len(g9)} pad={len(pad)})")


if __name__ == "__main__":
    main()
