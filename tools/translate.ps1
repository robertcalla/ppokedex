# Translates the Pokédex descriptions to Brazilian Portuguese with an offline model (Docker).
# Run tools\build_dex_data.py first (it exports the English texts), then this, then build_dex_data.py again.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
docker build -q -t pokedex-translate "$root\tools\translate" | Out-Null
docker run --rm -v "${root}:/project" pokedex-translate python /project/tools/translate/translate_pt.py
