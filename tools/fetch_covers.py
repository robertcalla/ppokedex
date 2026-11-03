"""Downloads a square (1:1) cover for every game version into romfs/covers/<version>.jpg.

    python tools/fetch_covers.py

Switch games use their official square eShop icon (via the titledb project). Older games use the box
art from libretro-thumbnails, centred over a blurred copy of itself so the image is square without
cropping the art. Downloads are cached in tools/cache/covers/.
"""
import io
import json
import pathlib
import re
import sys
import urllib.parse
import urllib.request

from PIL import Image, ImageEnhance, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / "cache" / "covers"
OUT = ROOT / "romfs" / "covers"
SIZE = 256
UA = "PokedexSwitchHomebrew/1.0"

SWITCH = {  # version id -> title id
    "letsgopikachu": "010003F003A34000", "letsgoeevee": "0100187003A36000",
    "sword": "0100ABF008968000", "shield": "01008DB008C2C000",
    "brilliantdiamond": "0100000011D90000", "shiningpearl": "010018E011D92000",
    "legendsarceus": "01001F5010DFA000", "scarlet": "0100A3D008C5C000",
    "violet": "01008F6008C5E000", "legendsza": "0100F43008C44000",
}

# version id -> (libretro-thumbnails repository, file name prefix to look for)
BOXART = {
    "red": ("Nintendo_-_Game_Boy", "pokemon - red version ("),
    "blue": ("Nintendo_-_Game_Boy", "pokemon - blue version ("),
    "yellow": ("Nintendo_-_Game_Boy", "pokemon - yellow version"),
    "gold": ("Nintendo_-_Game_Boy_Color", "pokemon - gold version ("),
    "silver": ("Nintendo_-_Game_Boy_Color", "pokemon - silver version ("),
    "crystal": ("Nintendo_-_Game_Boy_Color", "pokemon - crystal version ("),
    "ruby": ("Nintendo_-_Game_Boy_Advance", "pokemon - ruby version ("),
    "sapphire": ("Nintendo_-_Game_Boy_Advance", "pokemon - sapphire version ("),
    "emerald": ("Nintendo_-_Game_Boy_Advance", "pokemon - emerald version ("),
    "firered": ("Nintendo_-_Game_Boy_Advance", "pokemon - firered version ("),
    "leafgreen": ("Nintendo_-_Game_Boy_Advance", "pokemon - leafgreen version ("),
    "diamond": ("Nintendo_-_Nintendo_DS", "pokemon - diamond version ("),
    "pearl": ("Nintendo_-_Nintendo_DS", "pokemon - pearl version ("),
    "platinum": ("Nintendo_-_Nintendo_DS", "pokemon - platinum version ("),
    "heartgold": ("Nintendo_-_Nintendo_DS", "pokemon - heartgold version ("),
    "soulsilver": ("Nintendo_-_Nintendo_DS", "pokemon - soulsilver version ("),
    "black": ("Nintendo_-_Nintendo_DS", "pokemon - black version ("),
    "white": ("Nintendo_-_Nintendo_DS", "pokemon - white version ("),
    "black2": ("Nintendo_-_Nintendo_DS", "pokemon - black version 2 ("),
    "white2": ("Nintendo_-_Nintendo_DS", "pokemon - white version 2 ("),
    "x": ("Nintendo_-_Nintendo_3DS", "pokemon x ("),
    "y": ("Nintendo_-_Nintendo_3DS", "pokemon y ("),
    "omegaruby": ("Nintendo_-_Nintendo_3DS", "pokemon omega ruby ("),
    "alphasapphire": ("Nintendo_-_Nintendo_3DS", "pokemon alpha sapphire ("),
    "sun": ("Nintendo_-_Nintendo_3DS", "pokemon sun ("),
    "moon": ("Nintendo_-_Nintendo_3DS", "pokemon moon ("),
    "ultrasun": ("Nintendo_-_Nintendo_3DS", "pokemon ultra sun ("),
    "ultramoon": ("Nintendo_-_Nintendo_3DS", "pokemon ultra moon ("),
}


def get(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def cached(name: str, url: str) -> bytes:
    path = CACHE / name
    if not path.exists():
        path.write_bytes(get(url))
    return path.read_bytes()


def boxart_files(repo: str) -> list[str]:
    path = CACHE / f"{repo}.json"
    if not path.exists():
        tree = json.loads(get(f"https://api.github.com/repos/libretro-thumbnails/{repo}/git/trees/master?recursive=1"))
        names = [e["path"] for e in tree["tree"] if e["path"].startswith("Named_Boxarts/")]
        path.write_text(json.dumps(names), encoding="utf-8")
    return json.loads(path.read_text(encoding="utf-8"))


def pick(files: list[str], prefix: str) -> str | None:
    def score(f: str) -> tuple:
        n = f.split("/", 1)[1].lower()
        return ("(usa" not in n, "demo" in n or "beta" in n or "kiosk" in n, len(n))
    matches = [f for f in files if f.split("/", 1)[1].lower().startswith(prefix)]
    return min(matches, key=score) if matches else None


def switch_icons() -> dict[str, str]:
    path = CACHE / "switch_icons.json"
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    print("baixando a base de títulos do Switch (titledb, ~90 MB, só desta vez)...")
    db = json.loads(get("https://raw.githubusercontent.com/blawar/titledb/master/US.en.json"))
    wanted = set(SWITCH.values())
    icons = {}
    for v in db.values():
        tid = (v.get("id") or "").upper()
        if tid in wanted and v.get("iconUrl") and tid not in icons:
            icons[tid] = v["iconUrl"]
    path.write_text(json.dumps(icons), encoding="utf-8")
    return icons


def square(img: Image.Image) -> Image.Image:
    img = img.convert("RGB")
    if abs(img.width - img.height) <= 2:
        return img.resize((SIZE, SIZE), Image.LANCZOS)
    # Blurred, darkened copy as background, the whole cover centred on top.
    scale = max(SIZE / img.width, SIZE / img.height)
    bg = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)
    left, top = (bg.width - SIZE) // 2, (bg.height - SIZE) // 2
    bg = bg.crop((left, top, left + SIZE, top + SIZE)).filter(ImageFilter.GaussianBlur(10))
    bg = ImageEnhance.Brightness(bg).enhance(0.55)
    fit = min(SIZE / img.width, SIZE / img.height)
    fg = img.resize((round(img.width * fit), round(img.height * fit)), Image.LANCZOS)
    bg.paste(fg, ((SIZE - fg.width) // 2, (SIZE - fg.height) // 2))
    return bg


def main() -> None:
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    missing = []
    icons = switch_icons()
    for version, tid in SWITCH.items():
        url = icons.get(tid)
        if not url:
            missing.append(version)
            continue
        img = Image.open(io.BytesIO(cached(f"{version}.icon", url)))
        square(img).save(OUT / f"{version}.jpg", quality=88)
        print(f"{version}: ícone do eShop")
    for version, (repo, prefix) in BOXART.items():
        f = pick(boxart_files(repo), prefix)
        if not f:
            missing.append(version)
            continue
        url = f"https://raw.githubusercontent.com/libretro-thumbnails/{repo}/master/" + urllib.parse.quote(f)
        img = Image.open(io.BytesIO(cached(f"{version}.boxart", url)))
        square(img).save(OUT / f"{version}.jpg", quality=88)
        print(f"{version}: {f.split('/', 1)[1]}")
    if missing:
        print("sem capa:", ", ".join(missing))
        sys.exit(1)


if __name__ == "__main__":
    main()
