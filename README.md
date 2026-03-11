# Game Boy Emulator (C++)

Emulador do **Game Boy clássico (DMG-01)** em C++17, com arquitetura modular e foco em clareza de código.

## Comece em 1 minuto

```bash
cmake -S . -B build
cmake --build build
./build/gbemu --choose-rom
```

Se já tiver uma ROM:

```bash
./build/gbemu --rom caminho/para/jogo.gb
```

## O que já funciona

- Núcleo por componentes:
  - `CPU` (LR35902 com cobertura ampla)
  - `Bus/MMU` (mapa de memória principal)
  - `PPU` (timing de modos + background + janela + sprites)
  - `Timer` (`DIV`, `TIMA`, `TMA`, `TAC`)
  - `Joypad` (`FF00` + interrupção)
  - `Cartridge` com `ROM ONLY` e `MBC1`
- Loop de frame com **70224 ciclos/frame**
- Interrupções (IE/IF + vetores)
- DMA OAM por `FF46`
- Carregamento de ROM `.gb` com leitura de título
- Frontend em tempo real com SDL2 (quando disponível)
- Áudio básico no modo SDL2 (frame sequencer + mix estéreo)
- Modo headless com export de framebuffer para `frame.ppm`

## Requisitos

- CMake
- Compilador com suporte a C++17
- SDL2 (opcional, detectada automaticamente no build)

## Como rodar

Modo tempo real (SDL2):

```bash
./build/gbemu --rom caminho/para/jogo.gb
```

Atalho equivalente:

```bash
./build/gbemu caminho/para/jogo.gb
```

Sem `--rom`, o seletor de ROM abre automaticamente no modo SDL.
Busca de ROMs: primeiro em `./rom`, depois em `./roms`.

Modo headless:

```bash
./build/gbemu --rom caminho/para/jogo.gb --headless 300
```

Ajuste de buffer de áudio (útil no WSL):

```bash
./build/gbemu --rom caminho/para/jogo.gb --audio-buffer 2048
```

## Controles (SDL2)

- Setas: direcionais
- `Z`: botão `A`
- `X`: botão `B`
- `Enter`: `Start`
- `Backspace`: `Select`
- `P`: mutar/desmutar áudio
- `Space`: pausar/continuar
- `I`: ocultar/mostrar painel de memória
- `F3` ou `Ctrl+S`: salvar state
- `F5` ou `Ctrl+L`: carregar state
- `Esc`: sair

## Save state

- Slot único por ROM
- Arquivo salvo em `./states/<nome-da-rom>.state`
- Exemplo: `./states/PokemonYellow.state`
- `F3` sobrescreve o arquivo atual

## Painel de debug (SDL)

- Tela do jogo à esquerda
- Painel à direita com:
  - leituras recentes de memória (`ADDR:VAL`)
  - instrução atual (`PC + OP`) e próxima (`NP + OP`)
  - seção de sprites (OAM: `Y`, `X`, `Tile`, `Attr`)
- Clique em um sprite na OAM para fixar seleção, destacar na tela e ver preview no painel

## Testes

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Cobertura atual: instruções críticas da CPU (`DAA`, bloco `CB`, `CALL/RET`) e DMA para OAM.

## Estrutura do projeto

- `include/gb/*.hpp`: interfaces e tipos
- `src/*.cpp`: implementação
- `tests/*.cpp`: testes
- `roms/`: pasta sugerida para ROMs locais

## Próximos passos

1. Melhorar fidelidade de timing de CPU/PPU (ciclos e edge cases de hardware)
2. Evoluir o áudio para maior precisão de APU
3. Expandir MBCs (`MBC2`, `MBC3`, `MBC5`) e RAM com bateria
4. Ampliar a suíte com ROMs de validação (Blargg/mooneye)
