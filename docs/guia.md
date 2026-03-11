# Guia do Emulador (Explicado para Leigos)

Este guia explica, em linguagem simples, como o emulador funciona por dentro.
A ideia e voce entender o projeto mesmo sem ser especialista em emulacao.

## 1. O que este programa faz

O programa imita um Game Boy no seu computador.

Ele pega um arquivo de jogo (`.gb` ou `.gbc`) e simula o hardware do console:

- processador (CPU)
- memoria (RAM/VRAM/OAM)
- video (PPU)
- audio (APU)
- botoes (Joypad)
- cartucho (ROM + MBC)

Quando essas partes trabalham juntas, o jogo aparece na tela e responde ao teclado.

## 2. Estrutura de pastas (visao geral)

```text
include/gb/core/         # interfaces do nucleo
include/gb/app/          # interfaces da aplicacao
include/gb/app/frontend/ # interfaces do frontend SDL
src/core/                # implementacao do nucleo
src/app/                 # app principal + fluxo de execucao
src/app/frontend/        # modulos do frontend
tests/                   # testes automatizados
docs/                    # documentacao
rom/ ou roms/            # jogos
states/                  # saves persistentes
captures/                # screenshots
```

Resumo rapido:
- `core` = emulacao em si
- `app` = como o programa inicia/roda
- `frontend` = janela, teclado, menus e painel de debug

## 3. Fluxo completo (do comando ate o jogo)

Quando voce roda `./build/gbemu`, acontece isto:

1. `src/app/main.cpp` le os argumentos (`--rom`, `--headless`, etc).
2. Se nao houver ROM, abre o seletor grafico SDL.
3. A ROM e carregada no `GameBoy`.
4. Save interno (`.sav`) e RTC (`.rtc`) sao carregados se existirem.
5. O loop principal roda frame a frame.
6. A imagem e desenhada, o audio e tocado e o teclado e lido.
7. Ao fechar, dados persistentes sao gravados no disco.

## 4. Nucleo de emulacao (pasta core)

## 4.1 `Cartridge`

Arquivos:
- `include/gb/core/cartridge.hpp`
- `src/core/cartridge.cpp`

Responsabilidades:
- carregar ROM do disco
- expor leitura/escrita no espaco do cartucho
- tratar tipos de cartucho e mapeadores (MBC)
- salvar/carregar RAM de bateria (`.sav`)
- salvar/carregar estado de RTC (`.rtc`)
- detectar suporte CGB

Mappers suportados no codigo atual (com niveis diferentes de cobertura):
- ROM only
- MBC1
- MBC2
- MBC3 (+ RTC)
- MBC5
- fallbacks para HuC1/HuC3/MBC7/Pocket Camera

## 4.2 `Bus` (MMU)

Arquivos:
- `include/gb/core/bus.hpp`
- `src/core/bus.cpp`

Responsabilidades:
- roteia leitura/escrita por endereco (RAM, VRAM, I/O, cartucho)
- controla interrupcoes (`IE/IF`)
- faz DMA de OAM (`FF46`)
- integra PPU/APU/Timer/Joypad
- guarda historico de leituras para o painel de debug

## 4.3 `CPU`

Arquivos:
- `include/gb/core/cpu.hpp`
- `src/core/cpu.cpp`
- `src/core/cpu_alu.cpp`

Responsabilidades:
- executar instrucoes do LR35902
- atualizar registradores e flags
- tratar interrupcoes
- controlar estado de `halt`

Sem CPU correta, o jogo nao avanca.

## 4.4 `PPU`

Arquivos:
- `include/gb/core/ppu.hpp`
- `src/core/ppu.cpp`

Responsabilidades:
- desenhar a imagem (160x144)
- montar background, window e sprites
- controlar modos/timings de LCD
- gerar framebuffer monocromatico e, quando aplicavel, colorido (CGB)

## 4.5 `APU`

Arquivos:
- `include/gb/core/apu.hpp`
- `src/core/apu.cpp`

Responsabilidades:
- gerar samples de audio
- mistura estereo
- entregar buffer para o SDL tocar

## 4.6 `Timer` e `Joypad`

Arquivos:
- `include/gb/core/timer.hpp`, `src/core/timer.cpp`
- `include/gb/core/joypad.hpp`, `src/core/joypad.cpp`

Timer:
- emula `DIV`, `TIMA`, `TMA`, `TAC`
- dispara interrupcao de timer

Joypad:
- converte teclado em botoes do GB
- emula registrador `FF00`

## 4.7 `GameBoy` (orquestrador)

Arquivos:
- `include/gb/core/gameboy.hpp`
- `src/core/gameboy.cpp`
- `src/core/gameboy_state_io.cpp`

Responsabilidades:
- junta `Cartridge + Bus + CPU`
- oferece `runFrame()`
- salva e carrega save state completo

Pense nele como a "fachada" principal da emulacao.

## 5. Camada de aplicacao (pasta app)

## 5.1 `main.cpp`

Arquivo:
- `src/app/main.cpp`

Responsabilidades:
- parse de argumentos
- selecao entre modo SDL, headless ou rom-suite
- carregar ROM e saves internos
- voltar ao menu de ROM quando recebe retorno do frontend

## 5.2 Opcoes de linha de comando

Arquivos:
- `include/gb/app/app_options.hpp`
- `src/app/app_options.cpp`

Suporta:
- `--rom <arquivo>`
- `--choose-rom`
- `--headless [frames]`
- `--rom-suite <manifesto>`
- `--scale <n>`
- `--audio-buffer <256..8192>`

Tambem aceita ROM como argumento posicional:

```bash
./build/gbemu caminho/para/jogo.gb
```

## 5.3 Paths de persistencia

Arquivos:
- `include/gb/app/runtime_paths.hpp`
- `src/app/runtime_paths.cpp`

Essas funcoes padronizam onde cada coisa sera salva:

- `states/<rom>.state`
- `states/<rom>.sav`
- `states/<rom>.rtc`
- `states/<rom>.palette`
- `states/<rom>.filters`
- `captures/<rom>/...`

Isso evita espalhar arquivos pelo projeto.

## 5.4 Runner headless e rom-suite

Arquivos:
- `src/app/headless_runner.cpp`
- `src/app/rom_suite_runner.cpp`

Headless:
- roda sem janela SDL
- executa N frames
- exporta `frame.ppm`

ROM suite:
- le manifesto
- roda casos automaticos
- valida registradores/estado esperado

## 6. Frontend SDL (janela, menus e debug)

O frontend foi separado por modulos para melhorar legibilidade.

Arquivos principais:
- `src/app/frontend/realtime.cpp`
- `src/app/frontend/realtime_support.cpp`
- `src/app/frontend/debug_ui.cpp`
- `src/app/frontend/rom_selector.cpp`

## 6.1 Seletor de ROM

Arquivo:
- `src/app/frontend/rom_selector.cpp`

Como funciona:
- procura jogos em `./rom` e `./roms`
- espera subpastas por jogo
- exibe cards em grade (label + capa opcional)
- suporta mouse, teclado, rolagem e duplo clique

## 6.2 Loop realtime

Arquivo:
- `src/app/frontend/realtime.cpp`

O loop principal:
- processa eventos de teclado/mouse
- roda 1 frame (ou varios no fast forward)
- atualiza audio
- renderiza jogo e overlays
- desenha painel de debug quando habilitado

## 6.3 Fullscreen e escalonamento

Recursos:
- fullscreen com `F`
- menu de modo com `N`:
  - Crisp Fit (nitido com barras laterais)
  - Full Stretch
  - Full Stretch Sharp (com sharpen)

A renderizacao usa nearest neighbor para manter pixel art nitida.

## 6.4 Paletas e filtros

Recursos:
- menu de paleta com `V`
- opcoes DMG classico, Pocket e Color (quando suportado)
- filtro visual com `H`: none/scanline/lcd
- preferencia salva por ROM (`.palette`, `.filters`)

## 6.5 Painel de debug

Recursos atuais:
- lista de leituras de memoria recentes
- mini-disassembler de instrucoes
- watch de endereco com historico
- lista de sprites OAM com scroll
- clique em sprite para selecionar e destacar
- editor de memoria (`M`) em hexadecimal
- lock de escrita por frame (`K`)
- watchpoint (`W`)
- breakpoints (`B` + menu BP/WP com `D`)
- busca de memoria (`S`) com modos:
  - exact, greater, less, changed, unchanged

## 6.6 Timeline de frames (pausado)

Quando pausado:
- `Left`: volta 1 frame
- `Right`: avanca 1 frame

Internamente existe um historico de estados para navegar no tempo sem perder contexto.

## 7. Saves: diferenca importante

Existem 2 tipos de save:

1. Save state (`.state`):
- snapshot completo da maquina emulada
- salva tudo do hardware

2. Save interno do jogo (`.sav`):
- RAM com bateria do cartucho
- equivalente ao save normal do proprio jogo

No projeto os dois coexistem e sao persistidos em disco.

## 8. Testes

Arquivos em `tests/` cobrem:
- CPU
- cartridge
- bus
- perifericos
- estado e opcoes

Comandos:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Tambem existem suites por modulo (`gbemu_suite_cpu`, `gbemu_suite_ppu`, etc).

## 9. Onde comecar se voce quiser estudar o codigo

Ordem recomendada:

1. `src/app/main.cpp` (fluxo geral)
2. `include/gb/core/gameboy.hpp` e `src/core/gameboy.cpp`
3. `src/core/bus.cpp`
4. `src/core/cpu.cpp` e `src/core/cpu_alu.cpp`
5. `src/core/ppu.cpp`
6. `src/app/frontend/realtime.cpp`

Assim voce entende primeiro o caminho principal e depois aprofunda.

## 10. Resumo final em uma frase

O projeto transforma seu PC em um Game Boy virtual, com emulacao modular, frontend SDL com debug forte e persistencia completa de estado/saves.
