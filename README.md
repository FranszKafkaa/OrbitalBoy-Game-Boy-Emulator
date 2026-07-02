# OrbitalBoy

![Preview do emulador](image/image.png)

OrbitalBoy e um emulador em C++17 com foco em **Game Boy**, **Game Boy Color** e suporte a **Game Boy Advance** via core nativo mGBA. O projeto tambem inclui frontend SDL2, seletor de ROMs, save states, debug em tempo real e ferramentas auxiliares para testes.

## Recursos

- Game Boy DMG e Game Boy Color
- Game Boy Advance com core mGBA nativo por padrao
- Frontend SDL2 com menu superior, fullscreen, filtros, escala e capturas
- Save state e save interno por ROM
- Painel de debug com memoria, breakpoints, watchpoints e disassembly
- Seletor grafico de ROMs com capas opcionais
- Modo headless e suite de testes automatizados
- RunLab/MCP como ferramenta opcional de inspecao e automacao

## Build

### Dependencias

- CMake 3.16+
- Compilador C++17
- SDL2 para o frontend grafico
- SDL2_image opcional para capas no seletor
- mGBA instalado/disponivel no sistema para o core GBA nativo

### macOS/Linux

```bash
cmake -S . -B build -DGBEMU_USE_SDL2=ON
cmake --build build -j
```

### Windows

```powershell
cmake -S . -B build -G "Ninja" -DGBEMU_USE_SDL2=ON
cmake --build build --config Release
```

Se SDL2 nao estiver disponivel, o projeto ainda pode ser compilado em modo headless.

## Como Rodar

Abrir o seletor de ROMs:

```bash
./build/gbemu
```

Rodar uma ROM diretamente:

```bash
./build/gbemu --rom caminho/para/jogo.gb
./build/gbemu --rom caminho/para/jogo.gbc
./build/gbemu --system gba --rom caminho/para/jogo.gba
```

Forcar sistema:

```bash
./build/gbemu --system gb --rom caminho/para/jogo.gb
./build/gbemu --system gba --rom caminho/para/jogo.gba
```

Forcar hardware GB/CGB:

```bash
./build/gbemu --rom caminho/para/jogo.gb --hardware dmg
./build/gbemu --rom caminho/para/jogo.gbc --hardware cgb
```

Modo headless:

```bash
./build/gbemu --rom caminho/para/jogo.gb --headless 300
```

## ROMs no Seletor

O seletor procura primeiro em `./rom` e depois em `./roms`. Cada jogo pode ficar em uma pasta propria:

```text
roms/
  Pokemon Yellow/
    pokemon.gb
    capa.jpg
  Fire Emblem/
    Fire Emblem.gba
    capa.jpg
```

Extensoes reconhecidas:

- `.gb`
- `.gbc`
- `.gba`

Capas `.jpg` ou `.jpeg` sao opcionais.

Para baixar capas automaticamente de uma fonte aberta (`libretro-thumbnails`):

```bash
./build/gbemu --fetch-covers
```

O comando salva `capa.png` ao lado de cada ROM sem imagem. Para substituir capas existentes:

```bash
./build/gbemu --force-fetch-covers
```

## Controles

### Jogo

- Setas ou `WASD`: direcional
- `Z`, `J`, `K` ou `C`: A
- `X`, `U`, `I` ou `V`: B
- `Enter` ou `Space`: Start
- `Backspace` ou `Right Shift`: Select
- `Q`: L no GBA
- `E`: R no GBA

### Sessao

- `Space`: pausar/continuar no GB
- `P` ou `M`: mute
- `Tab` segurado: fast-forward
- `F`: fullscreen
- `N`: menu de escala em fullscreen
- `F3`: mostrar/ocultar barra superior
- `F9` ou `F12`: captura
- `Esc`: sair

### Save State

No menu superior:

- `SESSAO > SALVAR STATE`
- `SESSAO > CARREGAR STATE`

Atalhos principais no GB:

- `Ctrl+S`: salvar state
- `F5` ou `Ctrl+L`: carregar state

No GBA, save state usa o core mGBA nativo por padrao.

### Debug

- `I` ou `F1`: abrir/fechar painel de debug
- `F7`: step instruction no GBA com debug aberto
- `B`: breakpoint no PC atual
- `D`: menu de breakpoints
- `S`: busca de memoria
- `G`: ir para endereco de memoria no GBA
- `E`: editar byte de memoria no GBA
- `PageUp` / `PageDown`: navegar memoria
- `Home`: ir para memoria no PC atual

## Video

No fullscreen, `N` abre o menu de escala:

- `1`: Crisp Fit
- `2`: Full Stretch
- `3`: Upscale Sharp

Outros recursos:

- `H`: alternar filtro no GB
- `V`: menu de paleta no GB/CGB
- Capturas ficam em `captures/<rom>/`

## Persistencia

Arquivos gerados ficam em `states/`:

- `.state`: save state
- `.sav`: save interno do cartucho
- `.rtc`: relogio RTC quando aplicavel
- `.palette`: preferencia de paleta
- `.filters`: preferencia de filtro
- `.controls`: controles por ROM
- `global.controls`: controles globais
- `global.network`: preferencias de rede

## Testes

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
./build/gbemu_tests
```

Tambem e possivel usar CTest:

```bash
ctest --test-dir build --output-on-failure
```

Suite de ROMs:

```bash
./build/gbemu --rom-suite roms/tests/manifest.txt
```

## RunLab e MCP

RunLab/MCP e opcional e fica documentado separadamente:

- `docs/runlab-mcp-cpp.md`
- `tools/orbitalboy-mcp-cpp/README.md`

Build do adaptador:

```bash
cmake -S . -B build -DGBEMU_BUILD_MCP=ON
cmake --build build --target orbitalboy-mcp
```

## Documentacao

- `docs/guia.md`
- `docs/rom-suite.md`
- `docs/runlab-mcp-cpp.md`

## Estrutura

```text
include/gb/core/        nucleo do emulador
src/core/               implementacao dos cores
include/gb/app/         camada de aplicacao
src/app/                CLI, frontend e runtime
src/app/frontend/       frontend SDL2 e debug UI
tests/                  testes automatizados
tools/                  ferramentas auxiliares
docs/                   documentacao detalhada
```
