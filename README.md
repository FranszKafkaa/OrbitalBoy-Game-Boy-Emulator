# Game Boy Emulator (C++)

Projeto base de emulador do **Game Boy clássico (DMG-01)** em C++17, com arquitetura modular e foco nas funcionalidades principais.

## Funcionalidades principais implementadas

- Estrutura de emulação por componentes:
  - `CPU` (LR35902 com cobertura ampliada)
  - `Bus/MMU` (mapa de memória principal)
  - `PPU` (timing de modos + background + janela + sprites)
  - `Timer` (`DIV`, `TIMA`, `TMA`, `TAC`)
  - `Joypad` (registro `FF00` e interrupção)
  - `Cartridge` com suporte inicial a `ROM ONLY` e `MBC1`
- Loop de frame com timing de **70224 ciclos/frame**.
- Interrupções principais com flags IE/IF e vetores.
- DMA OAM por `FF46`.
- Carregamento de ROM `.gb` e extração do título.
- Frontend em tempo real com SDL2 (quando disponível no build).
- Áudio no modo SDL2 com frame sequencer básico (length/envelope/sweep) e mix estéreo.
- Modo headless com export de framebuffer em `frame.ppm`.

## CPU (status)

Inclui, entre outras, estas classes de instruções:

- `LD` 8-bit/16-bit principais.
- Aritmética e lógica (`ADD/ADC/SUB/SBC/AND/XOR/OR/CP`).
- Controle de fluxo (`JR/JP/CALL/RET/RST`, condicionais e não condicionais).
- Instruções de flags/ajuste (`DAA`, `CPL`, `SCF`, `CCF`).
- Rotates acumulador (`RLCA`, `RRCA`, `RLA`, `RRA`).
- Bloco `CB` genérico (`RLC/RRC/RL/RR/SLA/SRA/SWAP/SRL/BIT/RES/SET`).

## Build

```bash
cmake -S . -B build
cmake --build build
```

SDL2 é opcional e detectado automaticamente.

## Uso

Modo tempo real (SDL2):

```bash
./build/gbemu --rom caminho/para/jogo.gb
```

Para ajustar latência/estalos do áudio (útil no WSL):

```bash
./build/gbemu --rom caminho/para/jogo.gb --audio-buffer 2048
```

Se quiser escolher a ROM via interface SDL:

```bash
./build/gbemu --choose-rom
```

No modo SDL, se você não passar `--rom`, o seletor também abre automaticamente.
As ROMs são buscadas primeiro em `./rom`, depois em `./roms`.

Modo headless:

```bash
./build/gbemu --rom caminho/para/jogo.gb --headless 300
```

Atalho também aceito:

```bash
./build/gbemu caminho/para/jogo.gb
```

Atalhos SDL2:

- Direcionais: setas
- `A`: `Z`
- `B`: `X`
- `Start`: `Enter`
- `Select`: `Backspace`
- `P`: mutar/desmutar áudio (feedback na interface)
- `Space`: pausar/continuar emulação sem perder estado
- `I`: ocultar/mostrar painel de memória
- `F3`: salvar state (1 slot, sobrescreve o atual)
- `F5`: carregar state salvo
- `Ctrl+S`: salvar state (atalho alternativo ao F3)
- `Ctrl+L`: carregar state (atalho alternativo ao F5)
- Sair: `Esc`

Save state persistente:

- O slot único é salvo em arquivo com o mesmo nome da ROM e extensão `.state` (ex.: `PokemonYellow.state`).
- O arquivo fica em `./states/<nome-da-rom>.state`.
- `F3` sobrescreve esse arquivo.

Interface SDL:

- Jogo renderizado normalmente à esquerda.
- Painel lateral à direita com leituras recentes de memória (`ADDR:VAL`) e marcação por região.
- No painel também aparece a instrução executada (`PC` + `OP`) e a próxima instrução (`NP` + `OP`).
- O painel inclui seção de sprites (OAM), mostrando endereço e bytes `Y`, `X`, `Tile`, `Attr` de cada entrada.
- Clique em uma linha de sprite na seção OAM para fixar a seleção e ver detalhes extras no debug (`SY/SX`, flags, etc).
- Ao selecionar um sprite, ele é destacado visualmente no jogo e um preview do sprite é renderizado no painel de debug.

## Testes

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Testes atuais cobrem instruções críticas da CPU (`DAA`, bloco `CB`, `CALL/RET`) e DMA para OAM.

## Estrutura

- `include/gb/*.hpp`: interfaces e tipos dos componentes.
- `src/*.cpp`: implementação dos módulos.
- `tests/*.cpp`: testes de fumaça.
- `roms/`: pasta sugerida para ROMs locais.

## Próximos passos recomendados

1. Melhorar fidelidade de timing da CPU/PPU (bugs de ciclo e edge-cases de hardware).
2. Implementar APU (áudio).
3. Expandir MBCs (`MBC2/MBC3/MBC5`) e RAM com bateria.
4. Adicionar suíte de testes com ROMs de validação (Blargg/mooneye).
