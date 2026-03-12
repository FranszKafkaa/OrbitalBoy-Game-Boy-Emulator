# Guia Completo do Projeto GBEMU (explicado para leigos)

Atualizado em 2026-03-12.

Este documento foi escrito para voce entender o projeto inteiro com calma, incluindo:

- o que cada parte faz
- como as pecas conversam entre si
- quais teclas acionam cada funcionalidade
- como os dados sao salvos no disco
- como o modo debug funciona por dentro
- como a arquitetura multithread foi montada

## Nota de manutencao (2026-03-12)

Foi aplicada uma correcao de compilacao em `src/core/gameboy.cpp` no metodo `loadBootRomFromFile`.

Detalhe tecnico:

- a leitura do Boot ROM usava inicializacao que podia cair no erro de parse ambiguo do C++ (most vexing parse)
- agora o codigo separa iteradores `begin/end` e cria o `std::vector<u8>` sem ambiguidade
- o fast forward (`Tab`) foi ajustado para modo com pacing (sem burst de 6 frames por ciclo)
- o rewind interno foi otimizado para evitar travadas: saiu de `vector` com `erase(begin)` para fila com `pop_front` O(1)
- `runFrame` em modo de timing preciso ganhou guarda de ciclos para evitar loop infinito quando LCD estiver desligado
- tecla `F11` abre menu de remapeamento de controles (teclado e controle) com salvamento em arquivo
- barra superior no SDL virou menu clicavel por secoes (`SESSAO`, `IMAGEM`, `DEBUG`, `CONTROLES`)
- tecla `F3` agora mostra/oculta a barra superior
- itens da barra ficaram dinamicos por disponibilidade e com highlight de hover no mouse
- menus pop-up agora tem botao `X` clicavel para fechar (`Escala`, `Paleta`, `Controles`)
- controles agora persistem automatico entre sessoes com perfil por ROM + fallback global
- APU ganhou canal 4 (noise): registradores `FF20..FF23`, LFSR, envelope e roteamento em `NR51`
- HDMA CGB no `FF55` foi ajustado: modo geral continua imediato, modo H-Blank agora transfere 16 bytes por H-Blank
- linha de comando ganhou `--hardware auto|dmg|cgb` para ROM dual-mode
- `GameBoy` agora permite alternar o hardware emulado (DMG/CGB) em tempo de carga da ROM

Impacto:

- remove erro de build em compiladores mais estritos
- nao muda comportamento funcional da emulacao
- reduz sensacao de "pulo de frame" no fast forward
- reduz travadas no fast forward em sessoes longas (historico cheio de rewind)
- melhora usabilidade da interface com descoberta de atalhos na propria tela
- adiciona navegacao por menu de janela com dropdown clicavel
- reduz ambiguidade visual com feedback imediato de hover em secoes/itens
- melhora fechamento de overlays sem depender so de teclado
- evita perder remapeamento de controles ao fechar e abrir o emulador
- melhora fidelidade de audio em jogos que usam ruido/percussao (canal 4)
- melhora compatibilidade CGB em jogos que dependem de HDMA por H-Blank
- permite testar ROM dual-mode em DMG real sem precisar trocar de build

## 0. Como ler este guia

Para ficar didatico, cada secao segue este formato:

- O que e: resumo simples da parte
- Onde esta: arquivos principais
- Como funciona: ordem real de execucao
- O que voce pode mexer com seguranca: pontos comuns de manutencao

Assim voce consegue ligar cada linha do comportamento que ve na tela com um ponto do codigo.

## 1. Objetivo do emulador

O GBEMU imita o hardware de um Game Boy classico no computador.

Isso significa que ele tenta reproduzir:

- CPU (processador)
- Bus/MMU (roteamento de memoria)
- PPU (video)
- APU (audio)
- Timer
- Joypad
- Cartridge e mappers (MBC)

Quando esses blocos trabalham juntos, o jogo roda como se estivesse no console real.

## 2. Estrutura de pastas (visao de manutencao)

```text
include/gb/core/                        # interfaces do nucleo
include/gb/app/                         # interfaces da aplicacao
include/gb/app/frontend/                # interfaces SDL/debug
include/gb/app/frontend/realtime/       # modulos internos do loop realtime

src/core/                               # implementacao do nucleo
src/core/cartridge/mappers/             # mappers separados por arquivo
src/app/                                # inicializacao e modos de execucao
src/app/frontend/                       # janela, renderizacao, debug e seletor
src/app/frontend/realtime/              # classes auxiliares do realtime

tests/                                  # testes automatizados
docs/                                   # documentacao (este guia)
rom/ e roms/                            # biblioteca de jogos
states/                                 # saves persistentes
captures/                               # screenshots PPM
```

Regra pratica:

- `core` = emulacao
- `app` = fluxo de execucao
- `frontend` = interface SDL e ferramentas de debug

## 3. Fluxo completo do programa (passo a passo real)

### 3.1 Entrada principal

Onde esta:

- `src/app/main.cpp`

Ordem:

1. `main()` chama `parseAppOptions(...)` para ler argumentos.
2. Se vier `--rom-suite`, executa suite automatica e termina.
3. Se nao veio ROM e SDL esta habilitado, abre seletor grafico de ROM.
4. Se ainda nao tiver ROM, encerra com erro amigavel.
5. Se for modo SDL normal, chama `runRealtimeFlow(...)`.
6. Se for headless, roda `runHeadless(...)`.

### 3.2 Carregamento da ROM

`loadGame(...)` faz:

1. `gb.loadRom(romPath)`.
2. Loga titulo da ROM.
3. Resolve o hardware emulado (auto/DMG/CGB) e aplica no `GameBoy`.
4. Tenta carregar save interno (`.sav`).
5. Tenta carregar RTC (`.rtc`) quando aplicavel.

### 3.3 Volta para o menu sem fechar app

No realtime, se o usuario apertar `L` (sem Ctrl), o frontend retorna codigo `2`.

`runRealtimeFlow(...)` interpreta `2` como:

- abrir seletor de ROM novamente
- carregar novo jogo
- iniciar nova sessao

Isso permite trocar de jogo sem fechar o programa inteiro.

## 4. Linha de comando

Onde esta:

- `include/gb/app/app_options.hpp`
- `src/app/app_options.cpp`

Opcoes:

- `--rom <arquivo>`: define ROM.
- `--choose-rom`: forca abrir seletor SDL.
- `--headless [frames]`: roda sem janela.
- `--rom-suite <manifesto>`: executa suite de compatibilidade.
- `--scale <n>`: escala base da janela no modo janela.
- `--audio-buffer <256..8192>`: buffer solicitado para SDL audio.
- `--hardware <auto|dmg|cgb>`: seleciona o hardware emulado para ROM dual-mode.

Tambem aceita ROM como argumento posicional:

```bash
./build/gbemu ./roms/Mario/mario.gb
```

## 5. Persistencia em disco (o que fica salvo)

Onde esta:

- `include/gb/app/runtime_paths.hpp`
- `src/app/runtime_paths.cpp`

Para cada ROM, o projeto usa um nome base (`stem`) e grava em `states/`:

- `.state`: save state completo do emulador
- `.sav`: save interno do cartucho (bateria)
- `.rtc`: estado de relogio (MBC3/HuC3)
- `.palette`: preferencia da paleta visual
- `.filters`: preferencia de filtro visual
- `.controls`, `.cheats`, `.replay`: reservados

Capturas vao para:

- `captures/<nome-da-rom>/frame_YYYYMMDD_HHMMSS_mmm.ppm`

Observacao importante:

- Save state e diferente de save interno do jogo.
- O emulador salva os dois (quando aplicavel).

## 6. Seletor de ROM SDL (sem terminal)

Onde esta:

- `src/app/frontend/rom_selector.cpp`

### 6.1 Estrutura esperada de pasta

O seletor procura em `./rom` e `./roms`.

Cada jogo deve estar em uma subpasta:

```text
roms/
  Pokemon Yellow/
    pokemon_yellow.gb
    capa.jpg
```

Regras:

- usa o nome da subpasta como label
- pega o primeiro `.gb` ou `.gbc`
- pega a primeira imagem `.jpg/.jpeg` (se existir)
- se nao tiver imagem, mostra placeholder `NO IMG`

### 6.2 Grade responsiva

A grade calcula colunas com base na largura atual da janela:

- numero de colunas e dinamico
- gap horizontal e recalculado por linha
- cards ficam centralizados
- scroll vertical por linha

Isso evita desalinhamento quando redimensiona.

### 6.3 Controles do seletor

- Setas: navegar entre cards
- `PageUp/PageDown`: salto maior
- `Home/End`: inicio/fim
- `Enter`: abrir ROM selecionada
- Clique: seleciona
- Duplo clique: abre
- `Esc`: cancelar

## 7. Frontend SDL realtime (sessao de jogo)

Onde esta:

- `src/app/frontend/realtime.cpp`

Esse e o arquivo central da experiencia em tempo real.

### 7.1 Inicializacao grafica e anti-tearing

Passos principais no comeco da funcao `runRealtime(...)`:

1. Define hints SDL de VSync.
2. Inicializa `SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO`.
3. Cria janela.
4. Cria renderer com `SDL_RENDERER_PRESENTVSYNC`.
5. Tenta forcar vsync com `SDL_RenderSetVSync` (quando disponivel).
6. Loga aviso se renderer sem flag de vsync.
7. Cria textura principal RGB24 (160x144).

Efeito pratico:

- reduz tearing
- mantem sincronismo visual mais estavel

Limite:

- se o driver/plataforma nao respeitar vsync, ainda pode ocorrer tearing.

### 7.2 Inicializacao de audio

- Pede dispositivo com taxa da APU (`gb::APU::SampleRate`), 2 canais, S16.
- Se abrir, inicia audio.
- Se falhar, roda sem audio e mostra aviso.

### 7.3 Estados de sessao (flags)

Logo apos inicializar, o codigo cria flags principais:

- `paused`, `muted`, `fastForward`
- `showPanel` (debug panel)
- `showBreakpointMenu`
- `showScaleMenu`
- `showPaletteMenu`
- `fullscreen`
- `watchpointEnabled`
- `backToMenu`

Valores default relevantes:

- painel debug oculto (`showPanel=false`)
- menu BP/WP oculto (`showBreakpointMenu=false`)
- fullscreen desligado

### 7.4 Estado de debug e edicao de memoria

Estruturas importantes em runtime:

- `MemoryWatch`: endereco observado + historico + lock por frame
- `MemoryEditState`: editor de endereco/valor em hex
- `MemoryWriteUiState`: feedback de escrita pendente/ultima escrita
- `MemorySearchState`: snapshot de 64KB + resultados da busca
- `BreakpointEditState`: input de endereco para breakpoint

### 7.5 Pipeline multithread

O realtime usa 3 workers:

1. Thread EMU
2. Thread RENDER
3. Thread AUDIO

Comunicacao entre threads:

- `DroppingQueue<RawFramePacket, 3>` para frames brutos
- `DroppingQueue<RgbFramePacket, 3>` para frames convertidos
- `AudioRingBuffer` para samples de audio

Sincronizacao:

- `std::atomic` para flags rapidas
- `std::mutex` para secoes criticas do `GameBoy`

Logs de diagnostico no terminal:

- `[MT][EMU] ...`
- `[MT][REN] ...`
- `[MT][AUD] ...`

### 7.6 O que cada thread faz

#### Thread EMU

- roda `gb.runFrame()`
- incrementa contador de frames
- captura timeline (rewind)
- coleta samples da APU e empurra no ring buffer
- aplica lock de memoria por frame quando ativo
- detecta watchpoint e breakpoint
- usa politica de timing:
  - normal: ~59.7 fps (`16742 us`)
  - fast forward: ~3x com pacing, sem burst por ciclo

#### Thread RENDER

- recebe frame bruto (mono + color)
- escolhe pipeline de paleta:
  - CGB real (quando suportado e selecionado)
  - paleta mono (Classic/Pocket)
- aplica filtro visual (None/Scanline/LCD)
- envia frame RGB para a fila de saida

#### Thread AUDIO

- consome samples do `AudioRingBuffer`
- envia para `SDL_QueueAudio`
- zera fila quando pausado/mudo/fast-forward
- loga underruns e samples descartados

### 7.7 Loop principal de eventos

No loop principal (thread de UI), o codigo:

1. sincroniza estado com atomics
2. calcula FPS de debug
3. processa eventos SDL (teclado, mouse, texto, scroll)
4. aplica escritas de memoria pendentes
5. atualiza watch history
6. renderiza frame do jogo
7. desenha overlays e painel debug
8. apresenta frame (`SDL_RenderPresent`)

### 7.8 Encerramento seguro

Ao sair:

1. sinaliza `mtRunning=false`
2. fecha filas (`close()`)
3. espera `join()` das threads
4. libera texturas/renderer/janela/audio
5. grava `sav`, `rtc`, `palette`, `filters`

## 8. Modos de escala, fullscreen e nitidez

Onde esta:

- `include/gb/app/frontend/realtime_support.hpp`
- `src/app/frontend/realtime_support.cpp`

### 8.1 Modos

Enum `FullscreenScaleMode`:

- `CrispFit`: preserva proporcao, barras quando necessario
- `FullStretch`: ocupa tudo, sem preservar proporcao
- `FullStretchSharp`: ocupa tudo + filtro de sharpen

`computeGameBlitLayout(...)` calcula retangulo final do jogo conforme:

- resolucao de saida
- se painel debug esta visivel
- fullscreen on/off
- modo de escala escolhido

Tela sempre limpa com preto antes do draw:

- evita fundos coloridos indesejados nas barras

### 8.2 Menu de escala

- `N` abre menu de escala (somente em fullscreen)
- selecao por `Up/Down`, `1/2/3`, `Enter`

## 9. Paletas e filtros visuais

Onde esta:

- `realtime_support.cpp`

Paletas (`V`):

- Game Boy Classico (verde)
- Game Boy Pocket (cinza/branco)
- Game Boy Color (so quando ROM suporta CGB)

Filtros (`H`):

- None
- Scanline
- LCD

Persistencia:

- escolhas salvas por ROM em `.palette` e `.filters`

## 10. Debug UI (painel lateral)

Onde esta:

- `include/gb/app/frontend/debug_ui.hpp`
- `src/app/frontend/debug_ui.cpp`

O painel foi ajustado para layout responsivo e evitar sobreposicao.

### 10.1 Informacoes mostradas no topo

- status RUNNING/PAUSED
- status AUDIO-ON/MUTED
- FPS em tempo real
- PC/OP executado
- next PC/OP
- mini janela de bytes (disasm)

### 10.2 Box WATCH

Mostra:

- endereco observado
- valor atual em hex e decimal
- lock ON/OFF
- ultima escrita (ok/erro)
- grafico historico (janela circular)

### 10.3 Leituras de memoria recentes

- lista de acessos recentes vindos do `Bus`
- marcador colorido por regiao de memoria
- clique em linha move WATCH para aquele endereco

### 10.4 Secao de sprite selecionado

- endereco OAM do sprite
- Y/X bruto e convertido
- tile e atributos
- preview do sprite abaixo da tabela
- destaque visual do sprite no jogo (retangulo + cruz)

### 10.5 Lista de sprites (OAM)

- lista com scroll
- mostra papel/flags (ON/OFF, BG/TOP, flip, paleta)
- clique seleciona sprite

### 10.6 Menu BP/WP (oculto por padrao)

- tecla `D` mostra/oculta
- watchpoint ON/OFF
- adicionar/remover breakpoints
- lista de breakpoints ativos (limite 16)

### 10.7 Busca de memoria

- `S` abre/fecha overlay de busca (com painel aberto)
- modos: Exact, Greater, Less, Changed, Unchanged
- snapshot manual (`R`) para comparacao changed/unchanged
- editar valor alvo (`E`)
- executar busca (`Enter`)
- limpar resultados (`C`)
- selecionar resultado com clique (vira endereco WATCH)

Detalhes internos:

- snapshot cobre 0x0000..0xFFFF (64KB)
- guarda ate 4096 matches para UI
- tambem contabiliza total real de matches

## 11. Editor de memoria confiavel

### 11.1 Entrada manual

- `M` abre editor
- campo endereco (4 hex)
- campo valor (2 hex)
- `Tab` alterna campo
- `Enter` aplica
- `Esc` cancela

### 11.2 Escrita segura com fila

As escritas vao para uma fila (`QueuedMemoryWrite`) antes de aplicar.

Beneficios:

- evita corrida com thread de emulacao
- gera feedback visual claro
- permite bloquear escrita em regioes readonly

### 11.3 Regioes consideradas gravaveis

`likelyWritableAddress(...)` considera gravavel:

- `A000-BFFF` (RAM de cartucho, quando habilitada)
- `C000-FDFF` (WRAM/echo)
- `FE00-FE9F` (OAM)
- `FF00-FFFF` (IO/HRAM/IE)

Bloqueia por padrao:

- `0000-7FFF` (ROM/control banks)

### 11.4 Atalhos de edicao rapida

Com painel aberto:

- `[` e `]`: endereco WATCH -1 / +1
- `=`: incrementa byte do WATCH
- `-`: decrementa byte do WATCH
- `0`: zera byte do WATCH
- `K`: liga/desliga lock por frame

## 12. Rewind e frame stepping

Onde esta:

- `include/gb/app/frontend/realtime/frame_timeline.hpp`
- `src/app/frontend/realtime/frame_timeline.cpp`

`FrameTimeline` guarda historico de estados:

- capacidade maxima: 900 frames
- `stepBack`: volta 1 frame
- `stepForward`: avanca 1 frame
- `captureCurrent`: salva estado atual

Atalhos (somente pausado):

- seta `Left`: voltar frame
- seta `Right`: avancar frame

## 13. Save state, save interno e RTC

### 13.1 Save state

- `Ctrl+S`: salva em `states/<rom>.state`
- `F5` ou `Ctrl+L`: carrega `states/<rom>.state`
- fallback de leitura: `legacyStatePath` (mesma pasta da ROM)

### 13.2 Save interno do jogo

- carregado no inicio (`.sav`)
- salvo ao fechar sessao

### 13.3 RTC

- carregado no inicio (`.rtc`)
- salvo ao fechar sessao
- usado para cartuchos com relogio (MBC3/HuC3)

## 14. Atalhos completos (mapa rapido)

### 14.1 Controles de jogo (joypad)

- `Setas`: D-pad
- `Z`: botao A
- `X`: botao B
- `Enter`: Start
- `Backspace`: Select

### 14.2 Controles globais

- `Esc`: sair
- `Space`: pausar/continuar
- `P`: mute/unmute
- `Tab` (segurar): fast forward
- `F11`: menu de remapeamento de controles
- `F3`: mostrar/ocultar barra superior
- `F`: fullscreen on/off
- `N`: menu de escala (em fullscreen)
- `V`: menu de paleta
- `H`: ciclo de filtro visual
- `J`: ciclo de link cable (Off/Loop/Noise)
- `F9`: captura screenshot PPM
- `L` (sem Ctrl): voltar ao menu de ROM

### 14.3 Saves

- `Ctrl+S`: salvar state
- `F5` ou `Ctrl+L`: carregar state

### 14.4 Debug

- `I`: mostrar/ocultar painel debug
- `D`: mostrar/ocultar menu BP/WP
- `M`: abrir editor de memoria
- `S`: mostrar/ocultar busca de memoria (com painel aberto)
- `W`: watchpoint on/off
- `B`: toggle breakpoint no PC atual
- `[`/`]`: endereco WATCH -/+ 1
- `K`: lock por frame
- `=`/`-`/`0`: ++/--/zero no endereco WATCH
- `Left/Right` pausado: rewind/forward de frame

### 14.5 Menu de controles (`F11`)

Quando abre o menu:

- `Up/Down`: seleciona acao (RIGHT/LEFT/UP/DOWN/A/B/SELECT/START)
- `Left/Right`: escolhe campo (`KEYBOARD` ou `CONTROLLER`)
- `Enter`: entra em modo captura para redefinir o input
- `Del`/`Backspace`: limpa binding selecionado
- `R`: restaura padrao
- `S`: salva bindings no arquivo de controles
- `Esc` ou `F11`: fecha menu

O menu mostra feedback visual do que esta selecionado e salva em disco para persistir entre execucoes.
Tambem e possivel fechar pelo botao `X` no canto superior direito.

### 14.6 Barra superior

Durante a sessao SDL existe uma barra no topo com secoes clicaveis:

- `SESSAO`: pausar, mutar, fast forward, save/load, voltar ao menu de ROM, sair
- `IMAGEM`: fullscreen, escala, paleta, filtro, captura
- `DEBUG`: debug panel, BP/WP, busca de memoria
- `CONTROLES`: abrir menu de remapeamento

Comportamento:

- clique na secao abre/fecha dropdown
- clique em item executa acao imediatamente
- `Esc` fecha dropdown aberto
- `F3` oculta/mostra a barra inteira
- hover no mouse destaca secao e item
- itens aparecem quando disponiveis no contexto atual (por exemplo, `MENU ESCALA` apenas em fullscreen)

### 14.7 Fechamento por `X`

Os pop-ups de:

- escala
- paleta
- controles

possuem um `X` no topo para fechamento por mouse.
Os atalhos antigos (`Esc`, `N`, `V`, `F11`) continuam valendo.

### 14.8 Persistencia de controles

Quando voce muda qualquer binding no menu `F11`, o projeto grava imediatamente:

- `states/<rom>.controls`: configuracao da ROM atual
- `states/global.controls`: configuracao global de fallback

Carga ao iniciar:

1. tenta `states/<rom>.controls`
2. se nao existir, cai para `states/global.controls`

Assim o remapeamento continua nas proximas sessoes sem precisar configurar de novo.

## 15. Arquitetura de suporte multithread (novos modulos)

### 15.1 `DroppingQueue<T, MaxDepth>`

Arquivo:

- `include/gb/app/frontend/realtime/dropping_queue.hpp`

O que faz:

- fila thread-safe com tamanho maximo
- quando enche, descarta item mais antigo
- conta quantos itens foram descartados

Por que existe:

- evita travar pipeline quando uma thread fica mais lenta

### 15.2 `AudioRingBuffer`

Arquivos:

- `include/gb/app/frontend/realtime/audio_ring_buffer.hpp`
- `src/app/frontend/realtime/audio_ring_buffer.cpp`

O que faz:

- buffer circular thread-safe de samples de audio
- `push` escreve
- `pop` le com timeout
- `clear` limpa
- `close` acorda esperas e encerra

### 15.3 `FrameTimeline`

Arquivos:

- `include/gb/app/frontend/realtime/frame_timeline.hpp`
- `src/app/frontend/realtime/frame_timeline.cpp`

O que faz:

- historico de save states para navegar frame a frame

### 15.4 `session_models.hpp`

Arquivo:

- `include/gb/app/frontend/realtime/session_models.hpp`

O que centraliza:

- pacotes de frame bruto/RGB
- estado de busca
- estado de edicao de breakpoint
- escrita de memoria enfileirada

## 16. Nucleo de emulacao (core)

## 16.1 `GameBoy`

Arquivos:

- `include/gb/core/gameboy.hpp`
- `src/core/gameboy.cpp`
- `src/core/gameboy_state_io.cpp`

Papel:

- ponto principal para rodar frame
- organiza CPU, Bus e Cartridge
- exporta/importa save state completo

## 16.2 `Bus`

Arquivos:

- `include/gb/core/bus.hpp`
- `src/core/bus.cpp`

Papel:

- roteamento de leitura/escrita por endereco
- integracao com PPU/APU/Timer/Joypad/Cartridge
- eventos de leitura para debug
- controle de recursos CGB (VRAM bank, WRAM bank, HDMA, serial)

## 16.3 `CPU`

Arquivos:

- `include/gb/core/cpu.hpp`
- `src/core/cpu.cpp`
- `src/core/cpu_alu.cpp`

Papel:

- executa instrucoes LR35902
- gerencia registradores/flags/interrupcoes

## 16.4 `PPU`

Arquivos:

- `include/gb/core/ppu.hpp`
- `src/core/ppu.cpp`

Papel:

- gera framebuffer de 160x144
- suporta caminho mono e caminho color (CGB)

## 16.5 `APU`

Arquivos:

- `include/gb/core/apu.hpp`
- `src/core/apu.cpp`

Papel:

- gera samples de audio da emulacao
- inclui CH1, CH2, CH3 e CH4 (noise)
- CH4 usa LFSR para ruido e envelope de volume

## 16.6 `Timer` e `Joypad`

Arquivos:

- `include/gb/core/timer.hpp`, `src/core/timer.cpp`
- `include/gb/core/joypad.hpp`, `src/core/joypad.cpp`

Papel:

- timer: DIV/TIMA/TMA/TAC e interrupcao
- joypad: traduz teclado para registrador FF00

## 17. Cartridge e mappers (refatoracao recente)

Agora os mappers foram separados por arquivo para melhorar manutencao.

Arquivos principais:

- `src/core/cartridge.cpp` (orquestracao da classe `Cartridge`)
- `src/core/cartridge/mappers/factory.hpp`
- `src/core/cartridge/mappers/common.hpp`
- `src/core/cartridge/mappers/no_mbc.cpp`
- `src/core/cartridge/mappers/mbc1.cpp`
- `src/core/cartridge/mappers/mbc2.cpp`
- `src/core/cartridge/mappers/mbc3.cpp`
- `src/core/cartridge/mappers/mbc5.cpp`

### 17.1 O que ficou em `cartridge.cpp`

- abrir ROM
- detectar tipo de cartucho
- escolher mapper via factory
- salvar/carregar RAM
- salvar/carregar RTC
- expor metadados (titulo, CGB support, etc)

### 17.2 O que ficou em cada mapper

Cada arquivo de mapper implementa:

- leitura de ROM/RAM conforme regras do banco
- escrita de controle de banco
- serializacao de estado interno do mapper

Beneficio tecnico:

- arquivos menores
- menos classes no mesmo arquivo
- manutencao mais previsivel

## 18. CGB (Game Boy Color) e paletas

Comportamento atual:

- se ROM suporta CGB, existe modo de paleta colorida
- se nao suporta, usuario joga com paletas mono (Classic/Pocket)
- mudanca de paleta e em tempo real via menu `V`
- modo de hardware pode ser escolhido via CLI em ROM dual-mode (`--hardware`)
  - `auto`: preferencia CGB quando disponivel
  - `dmg`: forca comportamento de hardware DMG
  - `cgb`: forca comportamento CGB quando suportado
- HDMA (`FF55`) em CGB:
  - bit7=0: transferencia geral imediata
  - bit7=1: transferencia por H-Blank (16 bytes por entrada em H-Blank)

Observacao:

- modo CGB no projeto e funcional para varios casos, mas ainda experimental em termos de compatibilidade absoluta com toda biblioteca.

## 19. Captura de tela

- tecla `F9`
- salva como PPM RGB24
- caminho: `captures/<rom>/...`

## 20. Testes automatizados

Comandos:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Estado atual esperado:

- 1 executavel de testes (`gbemu_tests`)
- suites por modulo (`gbemu_suite_cpu`, `gbemu_suite_ppu`, etc)
- total de 10 testes registrados no CTest

## 21. Ordem sugerida para estudar o codigo (bem didatica)

1. `src/app/main.cpp`
2. `src/app/runtime_paths.cpp`
3. `src/app/frontend/rom_selector.cpp`
4. `src/app/frontend/realtime.cpp`
5. `src/app/frontend/debug_ui.cpp`
6. `src/app/frontend/realtime_support.cpp`
7. `src/core/gameboy.cpp`
8. `src/core/bus.cpp`
9. `src/core/cpu.cpp` e `src/core/cpu_alu.cpp`
10. `src/core/ppu.cpp`
11. `src/core/apu.cpp`
12. `src/core/cartridge.cpp` + `src/core/cartridge/mappers/*`

Se voce seguir nessa ordem, voce entende primeiro o "caminho do usuario" e depois aprofunda no hardware emulado.

## 22. Resumo final (em linguagem simples)

Este projeto transforma seu PC em um Game Boy virtual com:

- execucao realtime em SDL2
- seletor grafico de ROM
- debug avancado de memoria/sprites/breakpoints/watchpoints
- rewind frame a frame
- fast forward
- fullscreen com modos de escala
- paletas e filtros configuraveis
- saves persistentes (`.state`, `.sav`, `.rtc`)
- arquitetura multithread com logs
- codigo modularizado para facilitar manutencao

Se voce quiser evoluir o projeto, os melhores pontos de extensao hoje sao:

- novos filtros visuais
- melhorias no pipeline de audio
- mais cobertura de testes de compatibilidade
- expansao do suporte CGB por titulo
