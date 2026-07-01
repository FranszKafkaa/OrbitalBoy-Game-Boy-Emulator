# OrbitalBoy Game Boy Emulator (C++)

![Preview do emulador](image/image.png)

Emulador de **Game Boy (DMG)** com suporte inicial a **Game Boy Color (CGB)**, escrito em C++17.
Inclui tambem uma base **experimental de Game Boy Advance (GBA) - Fase 3** (parser + CPU ARM/Thumb essencial + timing/memoria + PPU expandida).
O projeto tem foco em legibilidade, arquitetura modular e ferramentas de debug em tempo real.

## Documentacao

- Guia completo: `docs/guia.md`
- Formato da suite de ROMs: `docs/rom-suite.md`
- RunLab MCP read-only: `docs/runlab-mcp-cpp.md`

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

Se SDL2 nao for encontrado, o build continua em modo headless automaticamente.

### Windows (PowerShell)

```powershell
cmake -S . -B build -G "Ninja" -DGBEMU_USE_SDL2=ON
cmake --build build --config Release
.\build\gbemu.exe
```

Sem SDL2 instalado no Windows, o executavel tambem sera gerado (headless).

Para compilar o adaptador MCP read-only do RunLab:

```bash
cmake -S . -B build -DGBEMU_BUILD_MCP=ON
cmake --build build --target orbitalboy-mcp
```

Para permitir controle por fila de input do RunLab/MCP, inicie o emulador explicitamente com:

```bash
./build/gbemu --rom caminho/para/jogo.gb --runlab-control --runlab-command-queue .runlab/commands.jsonl
```

Atalho equivalente pelo script da raiz:

```bash
./runlab.sh --emulator --rom caminho/para/jogo.gb
./runlab.sh --mcp
```

O modo `--emulator` grava automaticamente `.runlab/current-state.json`, `.runlab/current-screen.ppm` e consome `.runlab/commands.jsonl`.
O modo `--mcp` escreve na fila somente quando um cliente MCP chama uma tool de controle/anotacao, por exemplo `runlab_control_tap`.
Tambem ha tools para `runlab_control_pause`, `runlab_control_resume` e `runlab_control_step_frame`.
O MCP tambem grava heartbeats leves na fila para o painel de debug mostrar `MCP CLIENT` quando o adaptador esta vivo.
No menu superior do emulador, `DEBUG > RUNLAB MCP ON/OFF` liga ou desliga a ponte de fila no emulador.
Para controle por IA, use o prompt MCP `control_game_with_runlab`: ele observa com `runlab_get_control_context` e age com `runlab_control_macro`.
Para identificacao visual por IA, use `runlab_get_visual_context`; uma IA com visao pode ler `.runlab/current-screen.ppm` e chamar `runlab_visual_annotate` para desenhar caixas de player, inimigos e cenario na tela.
Com o painel de debug aberto, `Ctrl+T` abre uma caixa de texto no emulador; `Enter` grava o pedido em `.runlab/prompts.jsonl`. No cliente MCP, use o prompt `handle_prompt_box` ou a tool `runlab_get_pending_prompt` para a IA ler esse pedido, agir e finalizar com `runlab_ack_prompt`. O MCP escreve feedback em `.runlab/feedback.jsonl`, e o emulador mostra esse retorno na tela.
O adapter tambem tem um auto-runner simples para prompts como `ande pela direita`, `pule` e `passe de fase`; prompts livres ainda precisam de um cliente de IA.

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

### Ajuste fino de audio/performance no GBA realtime

O frontend realtime GBA usa fila de audio com watermarks e frame skip adaptativo orientado a audio.
Voce pode ajustar sem recompilar via variaveis de ambiente:

- `GBEMU_GBA_AUDIO_QUEUE_LOW_DIV`: limiar de fila baixa (padrao geral `12`)
- `GBEMU_GBA_AUDIO_QUEUE_TARGET_DIV`: alvo de fila (padrao geral `8`)
- `GBEMU_GBA_AUDIO_QUEUE_HIGH_DIV`: limiar de recuperacao (padrao geral `6`)
- `GBEMU_GBA_AUDIO_QUEUE_MAX_DIV`: limite maximo de fila para enfileiramento (padrao `4`)
- `GBEMU_GBA_AUDIO_QUEUE_AW_LOW_DIV`, `..._AW_TARGET_DIV`, `..._AW_HIGH_DIV`, `..._AW_MAX_DIV`:
  overrides especificos para perfil Advance Wars
- `GBEMU_GBA_AUDIO_SKIP_ENTRY_BUDGET_PERCENT`: percentual do budget de frame para armar skip adaptativo (padrao `105`)
- `GBEMU_GBA_AUDIO_SKIP_MAX_CONSECUTIVE`: maximo de frames pulados em sequencia (padrao `2`)
- `GBEMU_GBA_AUDIO_SKIP_COOLDOWN_FRAMES`: cooldown entre skips adaptativos (padrao `1`)
- `GBEMU_GBA_AUDIO_OUTPUT_SCALE`: ganho final do APU antes de clamp (`56.0` por padrao)
- `GBEMU_GBA_AUDIO_HIGHPASS_A`: coeficiente do high-pass de saida (`0.998` por padrao, `0` desliga)
- `GBEMU_GBA_AUDIO_LOWPASS_A`: coeficiente do low-pass de saida (`0.12` por padrao, `0` desliga)

Exemplo rapido (PowerShell):

```powershell
$env:GBEMU_GBA_AUDIO_QUEUE_AW_TARGET_DIV='6'
$env:GBEMU_GBA_AUDIO_SKIP_MAX_CONSECUTIVE='1'
$env:GBEMU_GBA_AUDIO_LOWPASS_A='0.10'
.\build\gbemu.exe '.\roms\Advance Wars\Advance Wars (USA).gba'
```

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

### RunLab Semantic Inspector

RunLab e uma camada semantica em cima do debugger existente para speedrun/TAS tooling. Ele nao substitui o visualizador de memoria, busca, watchpoints, breakpoints ou lista OAM: ele usa a selecao de sprites, o endereco observado e leituras de memoria ja expostos pelo painel de debug.

Com o painel de debug aberto:

- `Y`: cria/seleciona uma entidade RunLab a partir do sprite OAM selecionado. A primeira entidade recebe `player`; as proximas usam `entity_N`.
- `T`: alterna o tipo da entidade selecionada entre `Unknown`, `Player`, `Enemy`, `Item` e `Boss`.
- `U`: promove o endereco observado no watch atual para um `MemoryLabel`; se houver entidade selecionada, o label fica vinculado a ela.
- `F6`: captura snapshot RAM "before" para diff semantico.
- `F7`: compara a RAM atual contra o snapshot.
- `F8`: promove o primeiro endereco alterado do diff para `MemoryLabel`.
- `E`: exporta `profiles/<titulo_da_rom>.runlab.json`.
- `C`: roda o Correlation Scan, substitui a lista de leituras recentes pelos enderecos RAM candidatos e mostra os candidatos OAM em vermelho por cerca de 240 frames.
- Clique em um candidato RunLab: fixa o `WATCH` naquele endereco, apaga os demais candidatos vermelhos e mantem somente a entidade selecionada em vermelho forte ate `Q`.
- `M`: abre o reconhecimento RunLab no lugar da lista de memoria, mostrando player/itens/inimigos/cenario quando houver evidencias de entidades, OAM e labels de memoria.
- Clique em uma linha do reconhecimento: fixa o `WATCH` no endereco principal daquela linha e seleciona o sprite quando existir OAM associado.
- `Ctrl+M`: abre o editor manual de memoria antigo.
- `Ctrl+T`: abre a caixa de prompt RunLab MCP; `Enter` envia para `.runlab/prompts.jsonl` e `Esc` cancela.
- `1` / `2` / `3` / `4`: promove o melhor candidato atual como `player.x`, `player.y`, `camera_x` ou `state`.
- `G`: cria/inicia um objetivo padrao a partir dos labels existentes e captura baseline.
- `R`: recaptura baseline do objetivo ativo.
- `Q`: limpa a timeline de eventos RunLab e volta a lista de leituras recentes no lugar dos candidatos/reconhecimento.

O painel mostra a entidade selecionada, tipo, quantidade de labels vinculados, eventos importantes recentes, objetivo ativo, primeiro diff recente e sugestoes de correlacao para X/Y/camera/state. Tambem ha acoes `RUNLAB MCP ON/OFF` e `RUNLAB EXPORT` no menu superior `DEBUG`.

Event Detection transforma mudancas dos labels em eventos semanticamente nomeados. Exemplos: `lives`/`hp` diminuindo gera `damage_candidate`; chegando a zero gera `death_candidate`; `level_id`/`stage`/`room` mudando gera `level_change_candidate` e `split_candidate`; `goal_flag`/`clear_flag`/`finish_flag` indo de zero para nao-zero gera `goal_reached_candidate`. Mudancas de alta frequencia como `player.x` continuam registradas internamente, mas o painel prioriza eventos importantes.

Objetivos e splits usam condicoes estruturadas, sem parser de expressoes. `G` cria um objetivo padrao: se `level_id` existe, o objetivo e terminar a fase quando ele mudar desde a baseline; se `lives` existe, dano vira falha. Splits disparam uma unica vez contra a baseline capturada no inicio do objetivo. `R` recaptura essa baseline manualmente.

Correlation Scan observa a jogabilidade do usuario: com uma entidade selecionada, RunLab guarda as ultimas 120 amostras de posicao OAM e WRAM (`0xC000-0xDFFF`), depois ranqueia enderecos `u8` e `u16_le` cujos deltas parecem acompanhar a posicao de tela. O score `0.0..1.0` e somente uma heuristica, nao prova que o endereco e correto.

Fluxo recomendado para achar `player.x`/`player.y`: selecione um sprite OAM, crie a entidade com `Y`, marque-a como `Player` com `T`, mova o personagem por alguns segundos, pressione `C`, confira as sugestoes e promova os melhores candidatos com `1`/`2`.

Limitacoes do MVP: labels precisam existir antes das regras ficarem uteis, e nomes como `lives`, `level_id`, `player.x` e `goal_flag` importam para as heuristicas padrao. O OrbitalBoy nao sabe automaticamente qual sprite e o jogador. Posicao OAM nem sempre e a posicao logica do personagem; scroll de camera pode enganar a correlacao; entidades com multiplos sprites podem confundir o bbox; alguns jogos separam coordenadas em bytes ou usam sistemas maiores que `u16_le`. RunLab continua manual e observacional: nao ha IA, solver TAS, automacao de gameplay, cheats novos ou escrita de memoria especifica do RunLab.

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
