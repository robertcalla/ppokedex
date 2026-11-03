# Pokédex (homebrew para Nintendo Switch)

Enciclopédia Pokémon que lê os seus saves e mostra **quais Pokémon você tem em todos os jogos e quais
ainda faltam**, separados pela Pokédex Nacional e pelas Pokédex regionais.

- Lê automaticamente os saves dos jogos instalados no Switch (para o usuário escolhido ao abrir).
- Aceita saves de qualquer geração copiados para o cartão microSD (GB, GBC, GBA, DS, 3DS e backups de
  jogos de Switch). O app lembra onde cada arquivo está e o relê toda vez que é aberto.
- Se um arquivo registrado sumir, o app avisa e deixa você **procurar o arquivo em outro local do
  microSD** ou **excluir esse save do registro**.
- Cada Pokémon mostra em quais jogos ele está (caixas e equipe, quantos e se algum é shiny) e a
  descrição da Pokédex do último jogo em que ele aparece (texto do [pokemondb.net](https://pokemondb.net)).

## Jogos suportados

| Geração | Jogos | De onde vem o save |
|---|---|---|
| 1 | Red, Blue, Yellow (versões ocidentais) | `.sav` de emulador / cartucho |
| 2 | Gold, Silver, Crystal (versões ocidentais) | `.sav` |
| 3 | Ruby, Sapphire, Emerald, FireRed, LeafGreen | `.sav` |
| 4 | Diamond, Pearl, Platinum, HeartGold, SoulSilver | `.sav` / `.dsv` (512 KB) |
| 5 | Black, White, Black 2, White 2 | `.sav` |
| 6 | X, Y, Omega Ruby, Alpha Sapphire | arquivo `main` (Checkpoint/JKSM no 3DS) |
| 7 | Sun, Moon, Ultra Sun, Ultra Moon | arquivo `main` |
| 7 | Let's Go Pikachu / Eevee | **lido do console** ou `savedata.bin` |
| 8 | Sword / Shield, Brilliant Diamond / Shining Pearl, Legends: Arceus | **lido do console** ou backup |
| 9 | Scarlet / Violet, Legends: Z-A | **lido do console** ou backup |

São lidas as caixas do PC e a equipe; ovos não contam como registrados.
Não são suportados: versões japonesas de Gen 1/2, Colosseum/XD, Stadium e Pokémon HOME (os dados do
HOME ficam nos servidores da Nintendo, não no console).

## Instalação

1. Copie `pokedex.nro` para `sdmc:/switch/` no cartão microSD.
2. Abra pelo Homebrew Menu (pelo álbum ou segurando R ao abrir um jogo). Se o console tiver mais de
   um usuário, o app pergunta de qual usuário ler os saves.
3. Saves de outros consoles: copie para qualquer pasta do microSD (sugestão: `sdmc:/saves/`) e adicione
   pela tela **Saves** (botão Y). O app pergunta qual é o jogo e já deixa a opção mais provável marcada.

O registro dos saves externos fica em `sdmc:/switch/pokedex/saves.cfg` (um por linha:
`jogo|nome|caminho`).

## Controles

| Tela | Botão | Ação |
|---|---|---|
| Pokédex | ↑ ↓ / ← → / ZL ZR | Mover / pular página / pular 100 |
| | L / R | Trocar Pokédex (Nacional, Kanto, Johto, … Paldea, Lumiose) |
| | Y | Filtro: todos, registrados, faltando |
| | X | Ir para a tela Saves |
| | − | Reler todos os saves |
| Saves | Y | Adicionar save do microSD |
| | A | Trocar o arquivo de um save externo |
| | R | Remover do registro (o arquivo não é apagado) |
| | B | Voltar para a Pokédex |
| Qualquer | + | Sair |

## O peso das descrições

O texto de 1025 descrições ocupa uns **200 KB**, o que é praticamente nada perto dos 4 GB de RAM do
Switch. Mesmo assim, o app não carrega tudo:

- `tools/build_dex_data.py` gera um único `romfs/dex.bin` com os nomes, as listas das Pokédex e as
  descrições, mais um índice de posições.
- Ao abrir, o app carrega só os nomes e as listas (~30 KB).
- A descrição é lida do arquivo **só quando você seleciona o Pokémon**, uma de cada vez, e a última
  lida fica em cache.
- Os textos ficam embutidos no `.nro` (romfs), então não precisa de internet nem de arquivos extras.

O pokemondb.net é consultado **uma vez, no PC, na hora de gerar o `dex.bin`**, nunca pelo Switch. O
script respeita o intervalo de 2 s pedido no `robots.txt` do site e guarda tudo em `tools/cache/`, então
as próximas gerações são instantâneas. Se alguma página falhar, ele usa o texto em inglês do
[PokéAPI](https://pokeapi.co).

## Como compilar (Windows)

Requisitos: Python 3 e Docker Desktop (o build usa a imagem oficial `devkitpro/devkita64`, então não
precisa instalar o devkitPro).

```powershell
python tools\build_dex_data.py   # 1ª vez ~40 min (pokemondb.net); depois é instantâneo
.\build.ps1                      # gera pokedex.nro
```

`python tools\build_dex_data.py --no-pokemondb` gera em ~5 min usando só o PokéAPI.

Com o devkitPro instalado (pacotes `switch-dev switch-sdl2 switch-sdl2_ttf`), basta rodar `make`.

### Testar os saves no PC

```powershell
cmake -S . -B build-pc -G "MinGW Makefiles"; cmake --build build-pc
build-pc\pokedex_cli.exe --selftest                  # testes de criptografia/formatos
build-pc\pokedex_cli.exe caminho\do\save.sav         # lista os Pokémon (detecta o jogo)
build-pc\pokedex_cli.exe main sv --dex romfs\dex.bin # força o jogo e mostra os nomes
```

### Ver a interface no PC (sem Switch)

```powershell
docker build -t pokedex-preview tools/preview
python tools/make_test_save.py preview/sd/saves/main   # save sintético de X/Y
docker run --rm -v "${PWD}:/project" pokedex-preview sh /project/tools/preview/preview.sh "wait:2,shot:tela.bmp,quit"
```

O script aperta botões (`press:<n>`, numeração do SDL no Switch: 0=A 1=B 2=X 3=Y 6=L 7=R 13=↑ 15=↓) e
salva capturas em `preview/`.

## Estrutura

```
source/core/      leitura dos saves (sem dependência do Switch, compila no PC)
  pkm.cpp         descriptografia dos Pokémon (Gen 3 a 9)
  swish.cpp       SwishCrypto/SCBlock (Sword/Shield, Arceus, Scarlet/Violet, Z-A)
  games.cpp       layout de cada jogo, detecção de jogo pelo arquivo
  dexdata.cpp     leitura do dex.bin (descrições sob demanda)
  registry.cpp    saves externos (saves.cfg)
  collection.cpp  junta todos os saves: quem tem cada espécie
source/platform/  libnx: contas, montagem dos saves do console, fonte do sistema
source/ui/        interface em SDL2
tools/            gerador do dex.bin e das tabelas
pc/               ferramenta de linha de comando e plataforma de prévia para o PC
```

## Créditos e licença

Os formatos de save, as tabelas de conversão de espécies e a chave do SwishCrypto vêm do
[PKHeX](https://github.com/kwsch/PKHeX) (GPLv3). Por isso este projeto também é GPLv3.
As descrições são das Pokédex oficiais (© Nintendo / Game Freak / The Pokémon Company), obtidas pelo
pokemondb.net, para uso pessoal.
