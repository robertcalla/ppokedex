"""Builds the app data in romfs/: dex.bin (names, Pokédex lists, descriptions) and sprites/.

    python tools/build_dex_data.py                  # full build (first run: ~40 min for pokemondb.net)
    python tools/build_dex_data.py --no-pokemondb   # PokéAPI flavor text only (~5 min)

Pipeline
  1. PokéAPI (https://pokeapi.co): species, generations, regional and per-game dex lists.
  2. pokemondb.net: the Pokédex entry of the most recent game each species appears in. Requests
     honour the site's robots.txt crawl delay (2 s) and are cached in tools/cache/, so the site is
     hit once per species, ever. Species without an entry there fall back to PokéAPI's latest
     English flavor text.
  3. English texts are written to tools/cache/descriptions_en.json. tools/translate.ps1 turns them
     into Brazilian Portuguese (offline model) in tools/cache/descriptions_pt.json, which this
     script picks up on the next run. Without it, the English text is used.
  4. Pixel-art sprites (normal and shiny, 96x96) from the PokéAPI sprites repository.

The app keeps only names and dex lists in memory; descriptions and sprites are read from romfs one
at a time, when a Pokémon is shown, so the data size doesn't matter at runtime.
"""
import argparse
import html
import json
import pathlib
import re
import shutil
import struct
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / "cache"
ROMFS = ROOT / "romfs"
OUT = ROMFS / "dex.bin"
UA = "PokedexSwitchHomebrew/1.0 (personal project; one request per species)"
SPRITE_URL = "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/{shiny}{n}.png"

KIND_NATIONAL, KIND_REGION, KIND_GAME = 0, 1, 2

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

# (display name, save formats it covers (ids from games.cpp), PokéAPI dexes or a national range)
GAMES = [
    ("Red / Blue / Yellow", ["rby"], ["kanto"]),
    ("Gold / Silver / Crystal", ["gs", "crystal"], ["original-johto"]),
    ("Ruby / Sapphire / Emerald", ["rs", "emerald"], ["hoenn"]),
    ("FireRed / LeafGreen", ["frlg"], ["kanto"]),
    ("Diamond / Pearl", ["dp"], ["original-sinnoh"]),
    ("Platinum", ["pt"], ["extended-sinnoh"]),
    ("HeartGold / SoulSilver", ["hgss"], ["updated-johto"]),
    ("Black / White", ["bw"], ["original-unova"]),
    ("Black 2 / White 2", ["b2w2"], ["updated-unova"]),
    ("X / Y", ["xy"], ["kalos-central", "kalos-coastal", "kalos-mountain"]),
    ("Omega Ruby / Alpha Sapphire", ["oras"], ["updated-hoenn"]),
    ("Sun / Moon", ["sm"], ["original-alola"]),
    ("Ultra Sun / Ultra Moon", ["usum"], ["updated-alola"]),
    ("Let's Go, Pikachu! / Eevee!", ["lgpe"], ["letsgo-kanto"]),
    ("Sword / Shield", ["swsh"], ["galar", "isle-of-armor", "crown-tundra"]),
    ("Brilliant Diamond / Shining Pearl", ["bdsp"], range(1, 494)),
    ("Legends: Arceus", ["pla"], ["hisui"]),
    ("Scarlet / Violet", ["sv"], ["paldea", "kitakami", "blueberry"]),
    ("Legends: Z-A", ["za"], ["lumiose-city", "hyperspace"]),
]

TYPE_IDS = {"normal": 1, "fighting": 2, "flying": 3, "poison": 4, "ground": 5, "rock": 6, "bug": 7,
            "ghost": 8, "steel": 9, "fire": 10, "water": 11, "grass": 12, "electric": 13, "psychic": 14,
            "ice": 15, "dragon": 16, "dark": 17, "fairy": 18}
STAT_ORDER = ["hp", "attack", "defense", "special-attack", "special-defense", "speed"]

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
    return " / ".join(g for g in games if g), clean(text)


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
    return e["version"]["name"].replace("-", " ").title(), clean(e["flavor_text"])


def game_label(games: str) -> str:
    # pokemondb lists versions without the "Pokémon" prefix ("Scarlet / Violet").
    return games.replace("Pokémon ", "")


def dex_order(parts, count: int) -> list[int]:
    if isinstance(parts, range):
        return [n for n in parts if n <= count]
    order: list[int] = []
    for part in parts:
        d = cached_json(f"dex-{part}", f"https://pokeapi.co/api/v2/pokedex/{part}/")
        for e in sorted(d["pokemon_entries"], key=lambda e: e["entry_number"]):
            sid = int(e["pokemon_species"]["url"].rstrip("/").split("/")[-1])
            if sid not in order and sid <= count:
                order.append(sid)
    return order


def fetch_sprites(count: int) -> None:
    dest = ROMFS / "sprites"
    for shiny in ("", "shiny/"):
        cache_dir = CACHE / "sprites" / (shiny or "normal/")
        cache_dir.mkdir(parents=True, exist_ok=True)
        (dest / shiny).mkdir(parents=True, exist_ok=True)
        for n in range(1, count + 1):
            path = cache_dir / f"{n}.png"
            if not path.exists():
                raw = http_get(SPRITE_URL.format(shiny=shiny, n=n))
                if raw is None:
                    print(f"\nsprite {shiny}{n} não encontrado")
                    continue
                path.write_bytes(raw)
            shutil.copyfile(path, dest / shiny / f"{n}.png")
            print(f"\rsprites {shiny or 'normal/'}{n}/{count}", end="", flush=True)
    print()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-pokemondb", action="store_true", help="não consultar o pokemondb.net")
    ap.add_argument("--no-sprites", action="store_true", help="não baixar sprites")
    args = ap.parse_args()

    count = cached_json("species-index", "https://pokeapi.co/api/v2/pokemon-species/?limit=1")["count"]
    db = PokemonDB(not args.no_pokemondb)

    pt_path = CACHE / "descriptions_pt.json"
    pt = json.loads(pt_path.read_text(encoding="utf-8")) if pt_path.exists() else {}

    species, english = [], {}
    from_db = translated = 0
    for n in range(1, count + 1):
        s = cached_json(f"species-{n}", f"https://pokeapi.co/api/v2/pokemon-species/{n}/")
        name = next((x["name"] for x in s["names"] if x["language"]["name"] == "en"), s["name"].title())
        entry = db.get(s["name"])
        if entry:
            from_db += 1
        else:
            entry = pokeapi_flavor(s)
        game, text = entry if entry else ("", "")
        english[str(n)] = text
        tr = pt.get(str(n))
        if tr and tr.get("en") == text and tr.get("pt"):
            text = tr["pt"]
            translated += 1
        # Base stats and types of the default form.
        default = next(v["pokemon"]["url"] for v in s["varieties"] if v["is_default"])
        pid = int(default.rstrip("/").split("/")[-1])
        mon = cached_json(f"pokemon-{pid}", f"https://pokeapi.co/api/v2/pokemon/{pid}/")
        stats = {st["stat"]["name"]: st["base_stat"] for st in mon["stats"]}
        base = [min(255, stats.get(k, 0)) for k in STAT_ORDER]
        types = [TYPE_IDS.get(t["type"]["name"], 0) for t in sorted(mon["types"], key=lambda t: t["slot"])]
        types = (types + [0, 0])[:2]
        species.append((n, GEN_NUMBER.get(s["generation"]["name"], 0), name, text, game_label(game), base, types))
        if n % 50 == 0 or n == count:
            print(f"\rdados {n}/{count}", end="", flush=True)
    print()
    (CACHE / "descriptions_en.json").write_text(json.dumps(english, ensure_ascii=False, indent=0), encoding="utf-8")

    dexes = [("Nacional", KIND_NATIONAL, "", list(range(1, count + 1)))]
    for title, parts in REGIONS:
        dexes.append((title, KIND_REGION, "", dex_order(parts, count)))
    for title, games, parts in GAMES:
        dexes.append((title, KIND_GAME, ",".join(games), dex_order(parts, count)))

    if not args.no_sprites:
        fetch_sprites(count)

    write_bin(species, dexes)
    print(f"{OUT}: {OUT.stat().st_size / 1024:.0f} KB, {count} espécies, {len(dexes)} pokédex, "
          f"{from_db} descrições do pokemondb.net, {translated} traduzidas para pt-BR")
    if translated < count:
        print("Para traduzir as descrições, rode tools\\translate.ps1 e depois este script de novo.")


def write_bin(species, dexes) -> None:
    """Layout (little endian), version 3:
        "PKDX" u32 version u32 speciesCount u32 dexCount u32 stringsOffset
        species[] { u16 national, u8 generation, u8 pad, u32 name, u32 desc, u32 game,
                    u8 stats[6] (HP Atk Def SpA SpD Spe), u8 types[2] (1-18, 0 = none) }
        dex[]     { u32 name, u32 count, u32 listOffset, u32 kind, u32 games }
        u16 lists..., then UTF-8 strings (NUL terminated, offsets relative to stringsOffset)
    """
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
    for n, gen, name, text, game, base, types in species:
        table += struct.pack("<HBBIII6B2B", n, gen, 0, s(name), s(text), s(game), *base, *types)
    lists_start = header_size + len(table) + 20 * len(dexes)
    lists = bytearray()
    dex_table = bytearray()
    for title, kind, games, order in dexes:
        dex_table += struct.pack("<IIIII", s(title), len(order), lists_start + len(lists), kind, s(games))
        lists += struct.pack(f"<{len(order)}H", *order)
    strings_ofs = lists_start + len(lists)
    header = b"PKDX" + struct.pack("<IIII", 3, len(species), len(dexes), strings_ofs)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + table + dex_table + lists + blob)


if __name__ == "__main__":
    main()
