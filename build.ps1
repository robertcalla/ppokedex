# Builds pokedex.nro with the official devkitPro Docker image (no local devkitPro needed).
#   .\build.ps1          -> pokedex.nro
#   .\build.ps1 clean
param([string]$Target = "")

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if (-not (Test-Path "$root\romfs\dex.bin")) {
    Write-Warning "romfs\dex.bin nao existe. Rode: python tools\build_dex_data.py"
}

# The HOME shortcut loader (forwarder/) is built first: it is embedded in the app's romfs.
docker run --rm -v "${root}:/project" -w /project devkitpro/devkita64:latest `
    sh -c "make -C forwarder $Target && make -j8 $Target"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Target -eq "") { Write-Host "OK: $root\pokedex.nro" }
