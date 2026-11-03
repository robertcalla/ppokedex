"""Writes a synthetic Pokémon X/Y save (PK6 boxes) for testing the parser and the UI preview.

    python tools/make_test_save.py out/main [count]

The file only contains box Pokémon; everything else is zero.
"""
import random
import struct
import sys

BLOCK_POS = [
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
    2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1, 1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2,
    2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0,
]
INVERT = [0, 1, 2, 4, 3, 5, 6, 7, 12, 18, 13, 19, 8, 10, 14, 20, 16, 22, 9, 11, 15, 21, 17, 23]


def pk6(species: int, shiny: bool, rng: random.Random) -> bytes:
    d = bytearray(232)
    ec = rng.getrandbits(32)
    tid, sid = rng.getrandbits(16), rng.getrandbits(16)
    pid = rng.getrandbits(32)
    if shiny:
        pid = (pid & 0xFFFF) | (((tid ^ sid ^ (pid & 0xFFFF)) & 0xFFFF) << 16)
    struct.pack_into("<I", d, 0, ec)
    struct.pack_into("<HHHH", d, 8, species, 0, tid, sid)
    struct.pack_into("<I", d, 0x18, pid)
    struct.pack_into("<I", d, 0x74, rng.getrandbits(30))  # IVs, not an egg
    struct.pack_into("<H", d, 6, sum(struct.unpack_from("<112H", d, 8)) & 0xFFFF)
    sv = ((ec >> 13) & 31) % 24
    blocks = [d[8 + i * 56: 8 + (i + 1) * 56] for i in range(4)]
    order = BLOCK_POS[INVERT[sv] * 4: INVERT[sv] * 4 + 4]
    shuffled = b"".join(blocks[order[i]] for i in range(4))
    out = bytearray(d[:8]) + bytearray(shuffled)
    seed = ec
    for i in range(8, 232, 2):
        seed = (0x41C64E6D * seed + 0x6073) & 0xFFFFFFFF
        x = seed >> 16
        out[i] ^= x & 0xFF
        out[i + 1] ^= x >> 8
    return bytes(out)


def main() -> None:
    path = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 120
    rng = random.Random(42)
    save = bytearray(0x65600)
    box = 0x22600
    for slot in range(count):
        species = rng.randint(1, 721)
        save[box + slot * 232: box + (slot + 1) * 232] = pk6(species, rng.random() < 0.05, rng)
    with open(path, "wb") as f:
        f.write(save)
    print(f"{path}: {count} Pokémon")


if __name__ == "__main__":
    main()
