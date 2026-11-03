"""Translates the Pokédex descriptions to Brazilian Portuguese, offline.

Runs inside the tools/translate container (see translate.ps1). Reads tools/cache/descriptions_en.json
({"25": "text", ...}, written by build_dex_data.py) and writes tools/cache/descriptions_pt.json.
Already translated entries are kept, so only new or changed texts are translated.
"""
import glob
import json
import os
import re

import ctranslate2
import sentencepiece

CACHE = "/project/tools/cache"
SRC = f"{CACHE}/descriptions_en.json"
DST = f"{CACHE}/descriptions_pt.json"

# Terms the model tends to mistranslate; fixed after translation.
FIXES = [
    (r"\bpokémon\b", "Pokémon"),
    (r"\bPokemon\b", "Pokémon"),
    (r"\bPokébola\b", "Poké Bola"),
    (r"\bPoké Ball\b", "Poké Bola"),
]


def find_model() -> tuple[str, str]:
    model_dir = glob.glob("/model/**/model.bin", recursive=True)[0]
    sp_path = glob.glob("/model/**/sentencepiece.model", recursive=True)[0]
    return os.path.dirname(model_dir), sp_path


def split_sentences(text: str) -> list[str]:
    return [s for s in re.split(r"(?<=[.!?])\s+", text.strip()) if s]


def main() -> None:
    src = json.load(open(SRC, encoding="utf-8"))
    dst = json.load(open(DST, encoding="utf-8")) if os.path.exists(DST) else {}

    model_dir, sp_path = find_model()
    sp = sentencepiece.SentencePieceProcessor(model_file=sp_path)
    translator = ctranslate2.Translator(model_dir, device="cpu", inter_threads=4)

    todo = [k for k, v in src.items() if v and dst.get(k, {}).get("en") != v]
    print(f"{len(todo)} descrições para traduzir ({len(src) - len(todo)} já prontas)")

    sentences, owners = [], []
    for k in todo:
        for s in split_sentences(src[k]):
            sentences.append(sp.encode(s, out_type=str))
            owners.append(k)

    results = translator.translate_batch(sentences, beam_size=4, max_batch_size=64)
    parts: dict[str, list[str]] = {}
    for k, r in zip(owners, results):
        parts.setdefault(k, []).append("".join(r.hypotheses[0]).replace("▁", " ").strip())

    for k in todo:
        text = " ".join(parts.get(k, []))
        for pattern, repl in FIXES:
            text = re.sub(pattern, repl, text)
        dst[k] = {"en": src[k], "pt": text}

    json.dump(dst, open(DST, "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    print(f"ok: {DST}")


if __name__ == "__main__":
    main()
