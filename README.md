# Pokédex (homebrew para Nintendo Switch)

Enciclopédia e gerenciador de Pokémon que lê os seus saves e mostra **quais Pokémon você tem em todos os
jogos e quais ainda faltam**, com visual inspirado no [PKVault](https://github.com/Chnapy/PKVault).

Ao abrir, o app mostra um menu com as opções abaixo (ícones em pixel art de Pokédex, Poké Bola e Nintendo DS):

- **Pokédex**: Nacional, por **região** (Kanto … Lumiose), por **jogo** (Red/Blue/Yellow … Legends: Z-A)
  ou **Shinies** (só os shinies que estão nos saves e na Box Pessoal). Cada Pokémon aparece com o sprite
  em pixel art, os tipos, os **stats base**, os jogos em que ele está (caixa, equipe, quantidade, shiny) e
  a descrição da Pokédex **em português** do último jogo em que ele aparece. Na visão por jogo, só contam
  os Pokémon dos saves daquele jogo.
- **Armazenamento**: as caixas de todos os saves lado a lado com a **Box Pessoal**, um banco no cartão
  microSD. Dá para tirar Pokémon de um save, guardar na Box Pessoal e colocar em outro save do mesmo
  formato. O botão Y mostra a ficha completa do Pokémon (descrição, tipos e stats). Nada é gravado até
  você salvar, e antes de gravar o app faz backup de tudo.
- **Opções** (botão X no início): cria um **atalho no menu HOME** que abre o Pokedex direto, sem passar
  pelo Homebrew Menu (veja abaixo), e mostra onde ficam os arquivos do app.
- **Saves**: lê sozinho os saves dos jogos de Switch instalados e aceita saves de qualquer geração
  copiados para o microSD (GB, GBC, GBA, DS, 3DS e backups). O app lembra onde cada arquivo está. Se um
  arquivo sumir, ele avisa e deixa você **procurar o arquivo em outro local do microSD** ou **excluir o
  save do registro**.

Os saves aparecem com o nome do jogo e a capa quadrada (`white.sav` → **White**,
`Pokemon - Black Version 2 (USA, Europe).sav` → **Black 2**). O nome vem do conteúdo do save quando o
jogo guarda a versão (Black/White/Black 2/White 2, X/Y, OR/AS, Sun/Moon, US/UM e os jogos de Switch) e,
nos outros, do nome do arquivo. Um save já registrado com a versão errada é corrigido sozinho na
próxima abertura.

## Jogos suportados

| Geração | Jogos | Pokédex | Armazenamento |
|---|---|---|---|
| 1 | Red, Blue, Yellow (versões ocidentais) | ✔ | só visualização |
| 2 | Gold, Silver, Crystal (versões ocidentais) | ✔ | só visualização |
| 3 | Ruby, Sapphire, Emerald, FireRed, LeafGreen | ✔ | mover |
| 4 | Diamond, Pearl, Platinum, HeartGold, SoulSilver | ✔ | mover |
| 5 | Black, White, Black 2, White 2 | ✔ | mover |
| 6 | X, Y, Omega Ruby, Alpha Sapphire (arquivo `main` do Checkpoint/JKSM) | ✔ | mover |
| 7 | Sun, Moon, Ultra Sun, Ultra Moon | ✔ | só visualização¹ |
| 7 | Let's Go, Pikachu! / Eevee! (console) | ✔ | só visualização² |
| 8 | Sword, Shield, Brilliant Diamond, Shining Pearl, Legends: Arceus (console) | ✔ | mover |
| 9 | Scarlet, Violet, Legends: Z-A (console) | ✔ | mover |

¹ esses saves precisam de uma assinatura digital (MemeCrypto) que o app ainda não gera.
² o Let's Go guarda os Pokémon numa lista contínua que precisa ser reorganizada ao tirar um.

**Regras de movimento desta versão:** qualquer Pokémon de um save "mover" pode ir para a Box Pessoal, e
da Box Pessoal pode ir para qualquer save **do mesmo formato** (ex.: Scarlet ↔ Violet, Sword ↔ Shield,
Emerald ↔ FireRed, Diamond ↔ HeartGold). A conversão entre gerações (ex.: GBA → Switch) fica para a
próxima etapa.

Ovos não contam como registrados. Não são suportados: versões japonesas de Gen 1/2, Colosseum/XD,
Stadium e Pokémon HOME (os dados do HOME ficam nos servidores da Nintendo).

## Instalação

1. Copie `pokedex.nro` para `sdmc:/switch/` no cartão microSD.
2. Abra pelo Homebrew Menu (pelo álbum ou segurando R ao abrir um jogo). Se o console tiver mais de um
   usuário, o app pergunta de qual usuário ler os saves.
3. Saves de outros consoles: copie para qualquer pasta do microSD (sugestão: `sdmc:/saves/`) e adicione
   na tela **Saves** (botão Y).

Arquivos do app no microSD:

| Caminho | Conteúdo |
|---|---|
| `sdmc:/switch/pokedex/saves.cfg` | saves externos (`versão\|nome\|caminho`) |
| `sdmc:/switch/pokedex/bank/` | Box Pessoal: um arquivo por Pokémon (`b01s05.ek9` …, abre no PKHeX) + `bank.cfg` |
| `sdmc:/switch/pokedex/backups/<data>/` | cópia de cada save e da Box Pessoal antes de cada gravação |

## Atalho no menu HOME

Em **Opções → Atalho no menu HOME**, o app instala no próprio console um pequeno título ("forwarder")
com o ícone e o nome do Pokedex, que abre `sdmc:/switch/pokedex.nro` (o caminho de onde o app foi aberto).

- Precisa de **Atmosphère com sigpatches**, como qualquer forwarder.
- O título é gravado no cartão microSD. Pode ser removido em Opções ou em Configurações do sistema →
  Gerenciamento de dados.
- Se você mover o `.nro` para outra pasta, crie o atalho de novo a partir do novo local.

Como funciona: o código vem do instalador de forwarders do [Sphaira](https://github.com/ITotalJustice/sphaira)
(`owo.cpp`). O app monta três NCAs (programa, ícone/nome e registro) e as registra pelos serviços `ncm` e
`ns`. A chave do cabeçalho das NCAs é derivada no próprio console, então **não precisa de `prod.keys`**.
O programa é o carregador da pasta `forwarder/` (nx-hbloader modificado pelo Sphaira), que lê o caminho do
`.nro` gravado no título e o inicia.

## Controles

| Tela | Botão | Ação |
|---|---|---|
| Início | ← → / A | Escolher / abrir |
| Pokédex | ZL / ZR | Categoria: Nacional, Regiões, Jogos, Shinies |
| | L / R | Próxima região ou jogo |
| | ↑ ↓ / ← → | Mover / pular página |
| | Y | Filtro: todos, registrados, faltando |
| Armazenamento | A | Pegar / soltar o Pokémon |
| | L / R | Trocar a caixa do painel atual |
| | ZL / ZR | Trocar o save do painel da direita |
| | Y | Ficha do Pokémon (descrição, tipos, stats) |
| | X / − | Salvar / descartar os movimentos |
| Saves | Y | Adicionar save do microSD |
| | A | Trocar o arquivo de um save externo |
| | R | Remover do registro (o arquivo não é apagado) |
| Todas | B | Voltar (no Armazenamento, cancela o Pokémon carregado) |
| Início | X | Opções |
| Opções | A | Criar / remover o atalho no menu HOME |
| | + | Sair (pergunta antes se houver movimentos não salvos) |

## Dados: descrições, tradução e sprites

Tudo fica embutido no `.nro`, então o app não precisa de internet:

- `romfs/dex.bin` (~170 KB): nomes, listas das Pokédex e descrições. Ao abrir, o app carrega só os
  nomes e as listas (~30 KB). Cada descrição é lida do arquivo quando você seleciona o Pokémon.
- `romfs/sprites/` (~6 MB, normal e shiny, 96×96): carregados sob demanda, com no máximo 400 na memória.
- `romfs/covers/` (~1 MB): capa quadrada de cada versão.
- `romfs/ui/`: ícones do menu (Pokédex, Poké Bola e Nintendo DS em pixel art, 96×96).
- `romfs/forwarder/`: o carregador do atalho no menu HOME, compilado de `forwarder/` pelo `build.ps1`.

Como os dados são gerados:

1. `tools/build_dex_data.py` baixa espécies e listas do [PokéAPI](https://pokeapi.co) e a descrição do
   último jogo de cada Pokémon no [pokemondb.net](https://pokemondb.net). O site é consultado uma vez por
   espécie, respeitando o intervalo de 2 s do `robots.txt`, e tudo fica em cache em `tools/cache/`.
   Para os 17 Pokémon sem descrição no pokemondb.net (DLCs de Scarlet/Violet), ele usa o texto do PokéAPI.
2. `tools/translate.ps1` traduz as descrições para **português do Brasil** com um modelo offline
   (Argos Translate `en→pt-BR`, via Docker). É tradução automática; nomes de Pokémon e termos como
   "Poké Bola" são ajustados depois. Os sites oficiais em português bloqueiam acesso automatizado, por
   isso não foram usados.
3. Os sprites em pixel art vêm do repositório [PokeAPI/sprites](https://github.com/PokeAPI/sprites), e os
   stats base e os tipos, do PokéAPI.
4. `tools/fetch_covers.py` monta as capas 1:1. Jogos de Switch usam o ícone quadrado oficial do eShop
   (via [titledb](https://github.com/blawar/titledb)). Os outros usam a capa do
   [libretro-thumbnails](https://github.com/libretro-thumbnails), centralizada sobre uma versão desfocada
   dela mesma para ficar quadrada sem cortar nada.

## Como compilar (Windows)

Requisitos: Python 3 e Docker Desktop (o build usa a imagem oficial `devkitpro/devkita64`, então não
precisa instalar o devkitPro).

```powershell
python tools\build_dex_data.py   # dados em inglês + sprites (1ª vez ~40 min; depois é instantâneo)
.\tools\translate.ps1            # traduz as descrições (alguns minutos)
python tools\build_dex_data.py   # gera de novo, agora com o texto em pt-BR
python tools\fetch_covers.py     # capas dos jogos
.\build.ps1                      # gera pokedex.nro
```

Com o devkitPro instalado (pacotes `switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image`), basta
rodar `make`.

### Testar no PC

```powershell
cmake -S . -B build-pc -G "MinGW Makefiles"; cmake --build build-pc
build-pc\pokedex_cli.exe --selftest                  # criptografia, hashes e gravação de saves
build-pc\pokedex_cli.exe caminho\do\save.sav         # lista os Pokémon (detecta o jogo)
build-pc\pokedex_cli.exe main sv --dex romfs\dex.bin # força o jogo e mostra os nomes
```

### Ver a interface no PC (sem Switch)

```powershell
docker build -t pokedex-preview tools/preview
python tools/make_test_save.py preview/sd/saves/pokemon_y.sav   # save sintético de Y
docker run --rm -v "${PWD}:/project" pokedex-preview sh /project/tools/preview/preview.sh "wait:2,shot:tela.bmp,quit"
```

O script aperta botões (`press:<n>`, numeração do SDL no Switch: 0=A 1=B 2=X 3=Y 6=L 7=R 8=ZL 9=ZR
12=← 13=↑ 14=→ 15=↓) e salva as capturas em `preview/`. A pasta `preview/sd/` faz o papel do microSD.

## Estrutura

```
source/core/      lógica sem dependência do Switch (compila no PC)
  pkm.cpp         criptografia dos Pokémon (Gen 3 a 9)
  swish.cpp       SwishCrypto/SCBlock, leitura e gravação (Sword/Shield, Arceus, Scarlet/Violet, Z-A)
  games.cpp       layout de cada jogo, versões e detecção do jogo
  storage.cpp     caixas de cada save e regravação com checksums/hashes
  bank.cpp        Box Pessoal no microSD
  hash.cpp        CRC16, SHA-256, MD5
  dexdata.cpp     leitura do dex.bin (descrições sob demanda)
  registry.cpp    saves externos (saves.cfg)
  collection.cpp  junta todos os saves: quem tem cada espécie
source/platform/  libnx: contas, leitura e gravação dos saves do console, fonte do sistema, atalho no HOME
source/ui/        interface em SDL2 (tema, sprites, telas)
forwarder/        carregador usado pelo atalho no menu HOME (nx-hbloader, versão do Sphaira)
tools/            gerador de dados, tradução, tabelas, save de teste, prévia
pc/               ferramenta de linha de comando e plataforma de prévia para o PC
romfs/fonts/      Pixelify Sans (SIL Open Font License)
```

## Créditos e licença

Os formatos de save, os checksums, as tabelas de conversão de espécies e as chaves do SwishCrypto vêm do
[PKHeX](https://github.com/kwsch/PKHeX) (GPLv3), e o instalador do atalho no menu HOME, do
[Sphaira](https://github.com/ITotalJustice/sphaira) (GPLv3). Por isso este projeto também é GPLv3. O
carregador em `forwarder/` é o nx-hbloader (licença ISC, `forwarder/LICENSE.md`). O visual é
inspirado no [PKVault](https://github.com/Chnapy/PKVault). A fonte Pixelify Sans é distribuída sob a SIL
Open Font License (`romfs/fonts/OFL.txt`). As descrições e os sprites são de Pokémon
(© Nintendo / Game Freak / The Pokémon Company) e estão aqui para uso pessoal.
