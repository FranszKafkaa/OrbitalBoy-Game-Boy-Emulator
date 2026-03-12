# OrbitalBoy Game Boy Emulator (C++)

![Preview do emulador](image/image.png)

Emulador de **Game Boy (DMG)** com suporte inicial a **Game Boy Color (CGB)**, escrito em C++17.
Inclui tambem uma base **experimental de Game Boy Advance (GBA) - Fase 3** (parser + CPU ARM/Thumb essencial + timing/memoria + PPU expandida).
O projeto tem foco em legibilidade, arquitetura modular e ferramentas de debug em tempo real.

## Documentacao

- Guia completo: `docs/guia.md`
- Formato da suite de ROMs: `docs/rom-suite.md`

## Build

### Dependencias

- CMake 3.16+
- Compilador com C++17
- SDL2 (recomendado para frontend grafico)
- SDL2_image (opcional, para capas `.jpg/.jpeg` no seletor)

### Comandos

```bash
cmake -S . -B build -DGBEMU_USE_SDL2=ON
cmake --build build -j
```

Sem SDL2, o executavel ainda funciona em modo headless.

## Como rodar

### Fluxo padrao (SDL2)

```bash
./build/gbemu
```

Se nenhuma ROM for passada, abre o seletor grafico SDL automaticamente.

### Carregar ROM por parametro

```bash
./build/gbemu --rom caminho/para/jogo.gb
# ou
./build/gbemu caminho/para/jogo.gb

# selecionar hardware para ROM dual-mode
./build/gbemu --rom caminho/para/jogo.gb --hardware dmg
./build/gbemu --rom caminho/para/jogo.gb --hardware cgb

# modo GBA fase 3
./build/gbemu --system gba --rom caminho/para/jogo.gba
```

### Seletor de ROM explicitamente

```bash
./build/gbemu --choose-rom
```

### Headless

```bash
./build/gbemu --rom caminho/para/jogo.gb --headless 300
```

Gera `frame.ppm` ao final da execucao.

### Suite de compatibilidade

```bash
./build/gbemu --rom-suite roms/tests/manifest.txt
```

### Selecao de hardware (DMG/CGB)

- `--hardware auto` (padrao): usa CGB quando a ROM suporta
- `--hardware dmg`: forca modo Game Boy classico em ROM dual-mode
- `--hardware cgb`: forca modo Game Boy Color quando a ROM suporta

Regras:

- ROM CGB-only ignora `--hardware dmg`
- ROM sem suporte CGB ignora `--hardware cgb`

### Selecao de sistema (GB/GBA)

- `--system auto` (padrao): detecta por extensao (`.gba` => GBA; resto => GB)
- `--system gb`: forca fluxo de Game Boy
- `--system gba`: forca fluxo GBA (fase 3)

Estado atual do GBA fase 3:

- parser de ROM/header implementado
- CPU ARM7TDMI com base ARM + Thumb essencial implementada
- SWI HLE basica implementada (incluindo rotinas de divisao e copy/fill)
- memoria/timing com mapa principal + timers + IRQ regs + DMA (imediata/VBlank/HBlank) + keypad IRQ
- despacho de IRQ em HLE (vetor em `0x03007FFC`) e suporte a `HALT` basico no CPU
- PPU expandida: modo 0 com BG0..BG3 (text) + OBJ/sprites basicos; modos 3/4/5 com suporte a OBJ
- timing basico de scanline (`VCOUNT`) e blank flags (`DISPSTAT`) com pedido de IRQ de VBlank/HBlank/VCounter
- input de teclado mapeado para `KEYINPUT`
- ainda nao tem pipeline grafico completo (affine/rotacao, janelas, blending/efeitos), audio e save de cartucho GBA
- `--rom-suite` permanece focado em GB

### Netplay (UDP) e atraso configuravel

```bash
# host
./build/gbemu --rom jogo.gb --netplay-host 6100 --netplay-delay 2

# cliente
./build/gbemu --rom jogo.gb --netplay-connect 127.0.0.1:6100 --netplay-delay 2
```

- `--netplay-delay <0..10>` ajusta atraso de entrada para estabilizar partidas com latencia.
- quando um input remoto atrasado chega e diverge da previsao, o emulador aplica rollback simples.
- cada frame envia checksum para detectar dessync; ao detectar divergencia, a emulacao pausa com aviso visual.

## Estrutura esperada de ROMs (seletor SDL)

O seletor procura primeiro em `./rom`, depois em `./roms`.
Cada jogo deve estar em uma pasta propria:

```text
rom/
  Pokemon Yellow/
    pokemon.gb
    capa.jpg
  Mario Land/
    mario.gb
    capa.jpg
  Demo GBA/
    demo.gba
    capa.jpg
```

- Nome da pasta: vira o label no card
- `.gb`/`.gbc`/`.gba`: ROM
- `.jpg`/`.jpeg`: imagem de preview (opcional)

Se a ROM escolhida no seletor for `.gba`, o emulador entra automaticamente no fluxo GBA.

## Controles principais (SDL)

### Jogo

- Setas: direcional
- `Z`: A
- `X`: B
- `Enter`: Start
- `Backspace`: Select
- `Space`: pausar/continuar
- `Tab` (segurado): fast forward
- `P`: mute/unmute
- `Esc`: sair

### Estado e execucao

- `Ctrl+S`: salvar save state
- `F5` ou `Ctrl+L`: carregar save state
- `F11`: menu de remapeamento de controles (teclado e controle)
- `F3`: mostrar/ocultar barra superior
- `L`: encerrar sessao atual e voltar ao menu de ROMs
- `Left` (pausado): voltar 1 frame
- `Right` (pausado): avancar 1 frame

### Video

- `F`: fullscreen on/off
- `N` (em fullscreen): menu de escala
  - `1`: Crisp Fit (nitido + barras laterais)
  - `2`: Full Stretch (sem barras)
  - `3`: Full Stretch Sharp (sem barras + sharpen)
- `V`: menu de paleta
  - `1`: Game Boy Classico
  - `2`: Game Boy Pocket
  - `3`: Game Boy Color (somente ROM com suporte CGB)
- `H`: alternar filtro (`None` / `Scanline` / `LCD`)
- `F9`: screenshot `.ppm` em `captures/<rom>/`

### Debug

- `I`: mostrar/ocultar painel de debug (oculto por padrao)
- `D`: mostrar/ocultar menu visual de BP/WP (oculto por padrao)
- `S`: abrir/fechar busca de memoria
- `M`: editor hexadecimal (endereco + valor)
- `[` / `]`: endereco observado -1 / +1
- `=` / `-` / `0`: incrementar / decrementar / zerar endereco observado
- `K`: lock por frame no endereco observado
- `W`: watchpoint no endereco observado
- `B`: breakpoint no `PC` atual
- Clique em leitura de memoria: fixa watch
- Clique em sprite da lista OAM: seleciona sprite + preview/overlay

## Barra superior (SDL)

Durante o jogo, uma barra no topo mostra **secoes clicaveis**:

- `SESSAO`
- `IMAGEM`
- `DEBUG`
- `CONTROLES`
- `REDE`

Cada secao abre um dropdown com acoes (pause, mute, save/load, fullscreen, filtro, debug, etc.).

Comportamento da barra:

- itens aparecem dinamicamente quando estao disponiveis (ex.: `MENU ESCALA` somente em fullscreen)
- hover no mouse destaca secao e item
- clique executa a acao
- `F3` mostra/oculta a barra

No menu `REDE`:

- ciclar modo do link cable (equivalente a `J`)
- ajustar delay do netplay em tempo real (`0..10` frames)
- configuracao de rede fica persistida em `states/global.network`

## Fechar janelas pop-up

Menus pop-up (`Escala`, `Paleta`, `Controles`) agora exibem um botao `X` no canto superior direito:

- clique no `X` para fechar
- atalhos de teclado continuam funcionando normalmente

## Persistencia de controles

Ao alterar controles no menu `F11`, o emulador salva automaticamente para futuras sessoes:

- `states/<rom>.controls` (perfil da ROM atual)
- `states/global.controls` (fallback global)

No inicio da sessao, ele carrega primeiro o perfil da ROM; se nao existir, usa o global.

## Persistencia

Arquivos sao salvos em `./states/` por nome da ROM (`<rom>.ext`):

- `.state`: save state
- `.sav`: save interno do cartucho (battery RAM)
- `.rtc`: relogio RTC (MBC3/HuC3) com timestamp real para compensar tempo offline
- `.palette`: preferencia de paleta
- `.filters`: preferencia de filtro
- `global.network`: preferencias globais de link mode e netplay delay

Capturas vao para `./captures/<rom>/`.

## Arquitetura do projeto

- `include/gb/core/` e `src/core/`: nucleo de emulacao
  - CPU, Bus/MMU, PPU, APU, Timer, Joypad, Cartridge, GameBoy
  - `core/gba`: base do sistema GBA (fase 3)
- `include/gb/app/` e `src/app/`: camada de aplicacao
  - parsing de argumentos, runtime paths, modo headless, suite
- `include/gb/app/frontend/` e `src/app/frontend/`: frontend SDL modular
  - `rom_selector`, `realtime`, `debug_ui`, `realtime_support`
- `tests/`: testes automatizados

## Testes

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Suites por area (exemplo):

```bash
ctest --test-dir build -R gbemu_suite_cpu --output-on-failure
```
