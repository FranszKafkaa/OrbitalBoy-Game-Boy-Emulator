# Guia de arquitetura do OrbitalBoy

Este guia descreve o estado mantido do projeto. Mudancas historicas ficam em
`CHANGELOG.md` e no historico do Git.

## Escopo

OrbitalBoy oferece:

- emulacao de Game Boy e Game Boy Color pelo core proprio;
- emulacao de Game Boy Advance pelo backend nativo mGBA;
- frontend SDL2, modo headless e seletor de ROMs;
- saves, RTC, save states, filtros, controles e capturas;
- ferramentas de debug, netplay e integracao opcional RunLab/MCP.

O core GBA escrito neste repositorio e experimental. Ele nao participa do
executavel principal e so e compilado quando
`GBEMU_BUILD_EXPERIMENTAL_GBA=ON`.

## Builds suportados

Runtime completo com SDL2 e mGBA nativo:

```bash
cmake -S . -B build \
  -DGBEMU_ENABLE_GBA=ON \
  -DGBEMU_BUILD_EXPERIMENTAL_GBA=OFF \
  -DGBEMU_USE_SDL2=ON \
  -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Somente GB/GBC, sem SDL ou mGBA:

```bash
cmake -S . -B build-gb \
  -DGBEMU_ENABLE_GBA=OFF \
  -DGBEMU_BUILD_EXPERIMENTAL_GBA=OFF \
  -DGBEMU_USE_SDL2=OFF \
  -DBUILD_TESTING=ON
cmake --build build-gb -j
ctest --test-dir build-gb --output-on-failure
```

Core GBA experimental e seus testes:

```bash
cmake -S . -B build-experimental \
  -DGBEMU_ENABLE_GBA=OFF \
  -DGBEMU_BUILD_EXPERIMENTAL_GBA=ON \
  -DGBEMU_USE_SDL2=OFF \
  -DBUILD_TESTING=ON
cmake --build build-experimental -j
ctest --test-dir build-experimental --output-on-failure
```

Quando `GBEMU_ENABLE_GBA=ON`, a configuracao falha com uma mensagem clara se
headers ou biblioteca do mGBA nao estiverem instalados.

## Organizacao do codigo

```text
include/gb/core/                  interfaces do core GB/GBC
include/gb/core/gba/              mGBA e core GBA experimental
include/gb/app/                   aplicacao e fronteiras do frontend
include/gb/app/frontend/realtime/ modelos e sessoes do frontend
src/core/                         implementacao GB/GBC
src/core/gba/                     backends GBA
src/app/                          CLI, caminhos e inicializacao
src/app/frontend/                 SDL, seletor, debug e RunLab
src/app/frontend/realtime/        sessoes internas e loop em tempo real
tests/                            testes automatizados
tools/orbitalboy-mcp-cpp/         adaptador RunLab/MCP opcional
```

### Alvos CMake

- `gbcore`: core estavel de GB/GBC.
- `gbgba_mgba`: adaptador do backend mGBA; usa uma implementacao indisponivel
  controlada quando GBA esta desabilitado.
- `gbgba_experimental`: core GBA proprio, criado apenas com a opcao experimental.
- `gbfrontend_support`: opcoes, caminhos e modulos compartilhados pelo
  executavel e pelos testes.
- `gbemu`: aplicacao principal.
- `gbemu_tests`: testes do runtime suportado e, quando habilitado, do core
  experimental.

## Fluxo da aplicacao

1. `main.cpp` interpreta `AppOptions`.
2. O sistema alvo e resolvido por opcao ou extensao da ROM.
3. GB/GBC carrega `GameBoy`; GBA carrega `MgbaCore`.
4. No modo grafico, o frontend recebe um unico `RealtimeOptions`.
5. No modo headless, o core executa a quantidade solicitada de frames.
6. Saves e RTC sao persistidos no encerramento.

Nao existe selecao dinamica de outro backend GBA. O runtime GBA sempre usa
mGBA nativo.

## Frontend em tempo real

`runRealtime` e uma fronteira pequena: constroi `RealtimeSession` e executa a
sessao. As responsabilidades internas sao separadas:

- `SessionPersistence`: preferencias, controles e cheats.
- `RunLabSession`: caminhos, prompts, feedback e cadencia de exportacao.
- `NetplaySession`: atraso local, previsoes, historico, checksums e contadores.
- `DebugSession`: breakpoints, busca de memoria, watch e escrita pendente.
- `EmulationWorker`: inicio, parada idempotente e join dos workers.
- `SdlSessionView`: propriedade RAII de janela, renderer, texturas, audio e
  controle.

SDL continua sendo acessado no thread principal para eventos e desenho. Os
workers cuidam de emulacao, conversao de frames e audio. As filas limitadas
evitam crescimento de memoria quando um consumidor fica atrasado.

## Linha de comando

Opcoes principais:

- `--rom <arquivo>`: abre uma ROM.
- `--system <auto|gb|gba>`: seleciona o sistema.
- `--hardware <auto|dmg|cgb>`: escolhe hardware para ROM GB dual-mode.
- `--headless [frames]`: executa sem janela.
- `--scale <n>`: escala inicial.
- `--audio-buffer <256..8192>`: buffer de audio SDL.
- `--rom-suite <manifesto>`: executa uma suite de ROMs.
- `--netplay-delay <0..10>`: atraso local do netplay.
- `--runlab-control`: habilita a fila opcional de controle RunLab.

Exemplos:

```bash
./build/gbemu --rom roms/jogo.gb
./build/gbemu --system gba --rom roms/jogo.gba
./build/gbemu --rom roms/teste.gb --headless 300
```

## Persistencia

Arquivos por ROM ficam em `states/`:

- `.state`: save state;
- `.sav`: RAM persistente do cartucho;
- `.rtc`: relogio persistente;
- `.palette`: paleta;
- `.filters`: filtro;
- `.controls`: controles;
- `.cheats`: cheats.

Tambem existem:

- `states/global.controls`;
- `states/global.network`;
- `captures/<rom>/` para capturas;
- `.runlab/` para estado e filas do RunLab.

Replay nao faz parte do runtime.

## Controles essenciais

- Setas ou WASD: direcional.
- Z/J/K/C: A.
- X/U/I/V: B.
- Enter ou Space: Start.
- Backspace ou Shift direito: Select.
- Q/E: L/R no GBA.
- Space: pausa no GB/GBC.
- Tab: fast-forward.
- F: fullscreen.
- F3: barra superior.
- F9 ou F12: captura.
- I ou F1: painel de debug.
- Esc: sair.

Os menus visuais expõem save states, filtros, paleta, controles, rede e debug.

## Testes e verificacao

O comando padrao e:

```bash
ctest --test-dir build --output-on-failure
```

Para executar apenas uma suite durante desenvolvimento:

```bash
./build/gbemu_tests --suite cpu
./build/gbemu_tests --suite frontend
```

O script abaixo valida os contratos dos modos de build:

```bash
cmake -P tests/cmake/configure_matrix.cmake
```

A CI cobre Linux com runtime nativo, sanitizers e core experimental, macOS com
mGBA/SDL2, e Windows em modo GB/GBC-only.

## RunLab/MCP

O adaptador C++ e opcional:

```bash
cmake -S . -B build-mcp \
  -DGBEMU_ENABLE_GBA=OFF \
  -DGBEMU_BUILD_MCP=ON \
  -DBUILD_TESTING=ON
cmake --build build-mcp --target orbitalboy-mcp
```

Detalhes ficam em `docs/runlab-mcp-cpp.md`.

## Regras de manutencao

- Nao ligar o core GBA experimental ao executavel principal.
- Nao introduzir outro seletor de backend GBA.
- Preservar formatos de save, RTC, state, controles e rede.
- Manter logica sem SDL fora de blocos condicionais de SDL.
- Adicionar testes de caracterizacao antes de extrair comportamento do loop.
- Validar ao menos os modos nativo, GB-only e experimental antes de integrar.
