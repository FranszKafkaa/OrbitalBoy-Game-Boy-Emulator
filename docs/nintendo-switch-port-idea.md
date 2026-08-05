# Ideia de port do OrbitalBoy para Nintendo Switch

## Objetivo

Criar uma versão homebrew do OrbitalBoy para Nintendo Switch, distribuída em
formato `.nro`, usando o toolchain devkitPro, devkitA64, libnx e SDL2.

O objetivo inicial seria portar somente a emulação de Game Boy e Game Boy
Color. O suporte a Game Boy Advance via mGBA seria adicionado em uma segunda
etapa.

> Uma publicação oficial na Nintendo eShop exigiria acesso ao programa de
> desenvolvedores e ao SDK oficial da Nintendo. Esse fluxo é diferente do port
> homebrew descrito neste documento.

## Motivação técnica

O projeto já possui características favoráveis ao port:

- Core de emulação escrito em C++17.
- Separação entre o core e o frontend.
- Vídeo, áudio e eventos baseados em SDL2.
- Persistência de saves e configurações em arquivos comuns.
- Core de Game Boy e Game Boy Color sem dependências específicas de desktop.

Assim, não seria necessário reescrever a emulação. O trabalho principal ficaria
concentrado no sistema de build, frontend, controles, armazenamento e ciclo de
vida da aplicação no Switch.

## Plataforma proposta

- devkitPro e devkitA64 para compilação cruzada ARM64.
- libnx para integração com o ambiente homebrew do Nintendo Switch.
- `switch-sdl2` para vídeo, áudio e entrada.
- Pacote final em formato `.nro`, com ícone e metadados NACP.

Estrutura de distribuição sugerida:

```text
OrbitalBoy/
  OrbitalBoy.nro
  icon.jpg
  roms/
  states/
  captures/
  config/
```

No cartão SD, a aplicação usaria preferencialmente:

```text
sdmc:/switch/OrbitalBoy/OrbitalBoy.nro
sdmc:/switch/OrbitalBoy/roms/
sdmc:/switch/OrbitalBoy/states/
sdmc:/switch/OrbitalBoy/captures/
sdmc:/switch/OrbitalBoy/config/
```

## Primeira etapa: GB e GBC

O primeiro build para Switch desativaria componentes que aumentam a quantidade
de dependências ou não são necessários para validar o port:

```text
GBEMU_ENABLE_GBA=OFF
GBEMU_ENABLE_RETROACHIEVEMENTS=OFF
GBEMU_BUILD_MCP=OFF
BUILD_TESTING=OFF
```

O primeiro marco seria carregar uma ROM fixa, executar o core GB/GBC e exibir
os frames por SDL2. Depois seriam adicionados áudio, controles, seletor de ROMs
e persistência.

## Sistema de build

Seria criado um target ou preset específico para Switch, sem interferir nos
builds existentes para macOS, Linux e Windows.

Uma possível organização seria:

```text
platform/switch/
  main_switch.cpp
  switch_paths.cpp
  switch_input.cpp
  icon.jpg

cmake/
  SwitchBuild.cmake
```

O target usaria o toolchain do devkitPro, faria link estático com as bibliotecas
disponíveis para Switch e produziria os arquivos ELF e NRO necessários.

## Vídeo

O frontend SDL2 existente seria reaproveitado, com adaptações para a tela do
Switch:

- Saída em 1280 x 720 no modo portátil.
- Suporte futuro a 1920 x 1080 no dock, se adequado ao backend utilizado.
- Escala inteira e preservação da proporção original.
- Opções como `Crisp Fit`, `Full Stretch` e `Upscale Sharp`.
- Interface navegável sem mouse ou teclado.

Filtros mais complexos poderiam ser avaliados depois que o caminho básico de
renderização estivesse estável.

## Áudio

O áudio seria inicialmente conectado ao backend SDL2 do Switch, reaproveitando
o ring buffer e a lógica de sincronização existentes.

Os testes deveriam verificar:

- Ausência de estalos e underruns.
- Sincronização entre áudio e vídeo.
- Pausa e retomada da aplicação.
- Mudanças entre modo portátil e dock.
- Comportamento durante fast-forward.

## Controles

O frontend passaria a consumir eventos de game controller ou joystick do SDL2.
Um mapeamento inicial poderia ser:

| Switch | Game Boy / GBA |
|---|---|
| Direcional ou analógico esquerdo | Direcional |
| A | A |
| B | B |
| + | Start |
| - | Select |
| L | L no GBA |
| R | R no GBA |
| ZL | Abrir menu ou carregar state |
| ZR | Fast-forward |
| R3 | Abrir o debugger e pausar a emulação |

O mapeamento final deveria ser configurável e considerar Joy-Cons separados,
Joy-Cons acoplados e Pro Controller.

## Armazenamento

O sistema atual de caminhos relativos precisaria de uma implementação específica
para Switch, protegida por `#ifdef __SWITCH__` ou por uma abstração equivalente.

Os seguintes dados seriam persistidos no cartão SD:

- Saves internos dos cartuchos.
- Save states e miniaturas.
- Configuração de controles.
- Paletas e filtros.
- Capturas de tela.
- Preferências globais da aplicação.

O frontend não deveria tentar detectar a raiz do repositório ou depender do
diretório de trabalho no Switch.

## Interface

O seletor de ROMs existente poderia ser reaproveitado, desde que adaptado para
navegação integral por controle. A primeira versão teria uma interface simples:

1. Lista de ROMs encontradas no cartão SD.
2. Seleção de GB, GBC ou, futuramente, GBA.
3. Tela de jogo.
4. Menu de pausa com continuar, salvar, carregar, configurações e sair.

O suporte a touchscreen seria opcional e poderia ser implementado depois do
MVP.

## Debugger no Switch

O debugger é um dos principais diferenciais do OrbitalBoy e será requisito do
MVP, não um recurso opcional ou posterior. O port preservará as ferramentas
atuais de memória, busca e edição, breakpoints, watchpoints, disassembly,
inspeção de sprites, overlays e execução passo a passo.

O layout será híbrido e adaptativo:

- No dock ou quando houver espaço, jogo e debugger aparecem lado a lado.
- No modo portátil, o debugger usa abas em tela cheia para manter o texto
  legível.
- Qualquer aba poderá ser expandida para tela cheia.
- Um modo de acompanhamento permitirá observar dados enquanto o jogo roda.
- Entrar no modo de análise pausará o jogo por padrão.

As abas principais serão CPU/disassembly, memória, busca,
breakpoints/watchpoints e sprites. Os overlays de sprites e entidades
selecionadas continuarão disponíveis sobre a imagem do jogo.

O debugger não dependerá de teclado. Dentro dele, os controles serão
contextuais:

| Controle | Ação de debug |
|---|---|
| L / R | Alternar abas |
| Direcional | Navegar por linhas, endereços e campos |
| A | Selecionar ou confirmar |
| B | Voltar ou retornar ao jogo |
| X | Executar uma instrução |
| Y | Adicionar ou remover breakpoint |
| ZR | Avançar um frame ou paginar, conforme o contexto |
| ZL | Paginar ou acessar a ação secundária da aba |
| + | Abrir o menu de ações da aba |
| - | Alternar painel lateral e tela cheia |
| Analógico direito | Rolagem rápida |
| R3 | Fechar o debugger |

Endereços e valores serão inseridos por um editor hexadecimal próprio,
operável com direcional e botão A. Touchscreen e teclado USB poderão acelerar
a entrada, mas nunca serão obrigatórios. A interface mostrará uma legenda
contextual dos botões disponíveis.

Internamente, o debugger será separado em três responsabilidades:

- `DebugController`: converte ações abstratas em operações sobre a sessão.
- `DebugSnapshot`: fornece uma visão consistente do estado da emulação.
- `DebugView`: renderiza o painel desktop ou o layout adaptativo do Switch.

Essa separação permitirá reutilizar a lógica existente, manter paridade entre
desktop e Switch e testar comandos sem depender de SDL ou do hardware.

O design detalhado está em
[`docs/superpowers/specs/2026-08-05-nintendo-switch-debugger-design.md`](superpowers/specs/2026-08-05-nintendo-switch-debugger-design.md).

## Recursos inicialmente desativados

Para reduzir riscos e dependências, o primeiro port não incluiria:

- Download automático de capas.
- RetroAchievements.
- Netplay.
- RunLab e MCP.
- Diálogos e comportamentos específicos de desktop.

Esses recursos poderiam ser reavaliados individualmente depois que o port
básico estivesse estável.

## Segunda etapa: GBA

O suporte a GBA continuaria usando o core nativo mGBA. O mGBA já oferece suporte
ao Nintendo Switch e ao toolchain devkitA64, portanto o trabalho principal seria
produzir ou integrar uma biblioteca compatível com Switch e conectar o wrapper
`MgbaCore` existente.

Essa etapa incluiria:

- Build ARM64 da biblioteca mGBA.
- Link estático com o executável do OrbitalBoy.
- Vídeo 240 x 160.
- Mapeamento dos botões L e R.
- Save interno e save states.
- Testes de desempenho e sincronização de áudio.

## Etapas sugeridas

```text
Core GB/GBC compilando para ARM64
        ↓
Aplicação NRO iniciando no Switch
        ↓
Vídeo SDL exibindo frames
        ↓
Joy-Con e Pro Controller
        ↓
Áudio sincronizado
        ↓
ROM browser e persistência no SD
        ↓
Menu próprio para console
        ↓
Debugger completo adaptado ao controle
        ↓
Integração do mGBA
        ↓
Recursos online opcionais
```

## Marcos do projeto

### Marco 1 — Prova de conceito

- Gerar um `.nro`.
- Iniciar o OrbitalBoy no Switch.
- Carregar uma ROM GB fixa.
- Exibir frames e aceitar controles básicos.

### Marco 2 — MVP GB/GBC

- Seletor de ROMs.
- Áudio estável.
- Saves internos.
- Save states.
- Menu de pausa.
- Configuração básica de vídeo e controles.
- Debugger completo navegável por Joy-Con ou Pro Controller.
- Memória, disassembly, breakpoints, watchpoints e inspeção de sprites.

### Marco 3 — Polimento

- Ícone e metadados finais.
- Melhorias de interface.
- Capturas de tela.
- Tratamento correto do ciclo de vida da aplicação.
- Testes em modo portátil e dock.

### Marco 4 — GBA

- Integração do mGBA para Switch.
- Controles e saves de GBA.
- Testes de compatibilidade e desempenho.

## Pontos de atenção

- Compatibilidade de `std::filesystem` com o toolchain utilizado.
- Diferenças entre bibliotecas SDL2 desktop e Switch.
- Link estático e disponibilidade das dependências.
- Memória disponível conforme o modo de execução homebrew.
- Ciclo de suspensão, retomada e encerramento do aplicativo.
- Desempenho dos filtros e do frontend em modo portátil.
- Uso somente de ROMs e BIOS obtidas legalmente pelo usuário.

## Referências iniciais

- [devkitPro](https://github.com/devkitPro)
- [libnx](https://github.com/switchbrew/libnx)
- [Exemplos para Nintendo Switch](https://github.com/switchbrew/switch-examples)
- [mGBA](https://github.com/mgba-emu/mgba)
- [Anúncio do port do mGBA para Switch](https://mgba.io/2018/09/16/mgba-for-switch/)
