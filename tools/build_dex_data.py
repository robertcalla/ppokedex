"""Builds romfs/dex.bin: species names, regional Pokédex lists and descriptions.

    python tools/build_dex_data.py            # full build (first run takes ~40 min)
    python tools/build_dex_data.py --no-pokemondb   # PokéAPI flavor text only (~5 min)

Sources
  * PokéAPI (https://pokeapi.co): species, generations and regional dex lists.
  * pokemondb.net: the Pokédex entry from the most recent game each species appears in.
    Requests honour the site's robots.txt crawl delay (2 s) and every answer is cached in
    tools/cache/, so later builds are instant and the site is only hit once per species.
    If a page can't be read, the latest English PokéAPI flavor text is used instead.

The output is small (~250 KB). The app keeps names in memory and reads one description at a
time from the SD card, so the size of the text never matters at runtime.
"""
import argparse
import html
import json
import pathlib
import re
import struct
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "romfs" / "dex.bin"
UA = "PokedexSwitchHomebrew/1.0 (personal project; one request per species)"

# (display name, PokéAPI dex names merged in order)
REGIONS = [
    ("Kanto", ["kanto"]),
    ("Johto", ["updated-johto"]),
    ("Hoenn", ["updated-hoenn"]),
    ("Sinnoh", ["extended-sinnoh"]),
    ("Unova", ["updated-unova"]),
    ("Kalos", ["kalos-central", "kalos-coastal", "kalos-mountain"]),
    ("Alola", ["updated-alola"]),
    ("Galar", ["galar", "isle-of-armor", "crown-tundra"]),
    ("Hisui", ["hisui"]),
    ("Paldea", ["paldea", "kitakami", "blueberry"]),
    ("Lumiose", ["lumiose-city", "hyperspace"]),
]

GEN_NUMBER = {"generation-i": 1, "generation-ii": 2, "generation-iii": 3, "generation-iv": 4,
              "generation-v": 5, "generation-vi": 6, "generation-vii": 7, "generation-viii": 8,
              "generation-ix": 9}


def http_get(url: str, retries: int = 4) -> bytes | None:
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            time.sleep(2 + attempt * 3)
        except (urllib.error.URLError, TimeoutError):
            time.sleep(2 + attempt * 3)
    return None


def cached_json(name: str, url: str) -> dict:
    path = CACHE / "pokeapi" / f"{name}.json"
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    raw = http_get(url)
    if raw is None:
        sys.exit(f"falha ao baixar {url}")
    data = json.loads(raw)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")
    return data


def clean(text: str) -> str:
    text = html.unescape(re.sub(r"<[^>]+>", "", text))
    return re.sub(r"\s+", " ", text.replace("­", "")).strip()


def latest_pokemondb_entry(page: str) -> tuple[str, str] | None:
    start = page.find("Pokédex entries</h2>")
    if start < 0:
        return None
    end = page.find("</table>", start)
    rows = re.findall(r"<th>(.*?)</th>\s*<td class=\"cell-med-text\">(.*?)</td>", page[start:end], re.S)
    if not rows:
        return None
    games_html, text = rows[-1]
    games = [clean(g) for g in re.split(r"<br\s*/?>", games_html)]
    return " / ".join("Pokémon " + g for g in games if g), clean(text)


class PokemonDB:
    def __init__(self, enabled: bool):
        self.enabled = enabled
        self.path = CACHE / "pokemondb.json"
        self.data = json.loads(self.path.read_text(encoding="utf-8")) if self.path.exists() else {}
        self.last = 0.0

    def get(self, slug: str) -> tuple[str, str] | None:
        if slug in self.data:
            v = self.data[slug]
            return tuple(v) if v else None
        if not self.enabled:
            return None
        wait = 2.0 - (time.time() - self.last)
        if wait > 0:
            time.sleep(wait)
        self.last = time.time()
        raw = http_get(f"https://pokemondb.net/pokedex/{slug}")
        entry = latest_pokemondb_entry(raw.decode("utf-8", "replace")) if raw else None
        self.data[slug] = list(entry) if entry else None
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.data, ensure_ascii=False, indent=0), encoding="utf-8")
        return entry


def pokeapi_flavor(species: dict) -> tuple[str, str] | None:
    entries = [e for e in species.get("flavor_text_entries", []) if e["language"]["name"] == "en"]
    if not entries:
        return None
    e = entries[-1]
    game = "Pokémon " + e["version"]["name"].replace("-", " ").title()
    return game, clean(e["flavor_text"])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-pokemondb", action="store_true", help="não consultar o pokemondb.net")
    args = ap.parse_args()

    count = cached_json("species-index", "https://pokeapi.co/api/v2/pokemon-species/?limit=1")["count"]
    db = PokemonDB(not args.no_pokemondb)

    species = []
    from_db = 0
    for n in range(1, count + 1):
        s = cached_json(f"species-{n}", f"https://pokeapi.co/api/v2/pokemon-species/{n}/")
        name = next((x["name"] for x in s["names"] if x["language"]["name"] == "en"), s["name"].title())
        entry = db.get(s["name"])
        if entry:
            from_db += 1
        else:
            entry = pokeapi_flavor(s)
        game, text = entry if entry else ("", "")
        species.append((n, GEN_NUMBER.get(s["generation"]["name"], 0), name, text, game))
        print(f"\r{n}/{count} {name:<16}", end="", flush=True)
    print()

    dexes = [("Nacional", list(range(1, count + 1)))]
    for title, parts in REGIONS:
        order = []
        for part in parts:
            d = cached_json(f"dex-{part}", f"https://pokeapi.co/api/v2/pokedex/{part}/")
            for e in sorted(d["pokemon_entries"], key=lambda e: e["entry_number"]):
                sid = int(e["pokemon_species"]["url"].rstrip("/").split("/")[-1])
                if sid not in order and sid <= count:
                    order.append(sid)
        dexes.append((title, order))

    write_bin(species, dexes)
    print(f"{OUT}: {OUT.stat().st_size / 1024:.0f} KB, {count} espécies, {len(dexes)} pokédex, "
          f"{from_db} descrições do pokemondb.net")


def write_bin(species, dexes) -> None:
    blob = bytearray()
    offsets: dict[str, int] = {}

    def s(text: str) -> int:
        if not text:
            return 0xFFFFFFFF
        if text not in offsets:
            offsets[text] = len(blob)
            blob.extend(text.encode("utf-8") + b"\0")
        return offsets[text]

    header_size = 20
    table = bytearray()
    for n, gen, name, text, game in species:
        table += struct.pack("<HBBIII", n, gen, 0, s(name), s(text), s(game))
    lists_start = header_size + len(table) + 12 * len(dexes)
    lists = bytearray()
    dex_table = bytearray()
    for title, order in dexes:
        dex_table += struct.pack("<III", s(title), len(order), lists_start + len(lists))
        lists += struct.pack(f"<{len(order)}H", *order)
    strings_ofs = lists_start + len(lists)
    header = b"PKDX" + struct.pack("<IIII", 1, len(species), len(dexes), strings_ofs)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + table + dex_table + lists + blob)


if __name__ == "__main__":
    main()
