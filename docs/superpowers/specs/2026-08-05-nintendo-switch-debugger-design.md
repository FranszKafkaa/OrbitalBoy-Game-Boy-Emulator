# Design do debugger do OrbitalBoy no Nintendo Switch

## Objetivo

Preservar no port homebrew para Nintendo Switch todo o debugger atual do
OrbitalBoy, incluindo memória, busca e edição, breakpoints, watchpoints,
disassembly, sprites, overlays e execução passo a passo. O debugger será parte
obrigatória do MVP e funcionará integralmente com Joy-Con ou Pro Controller.

## Princípios

- O debugger é um recurso central do produto, não uma ferramenta opcional.
- Nenhuma operação deve exigir teclado, mouse ou touchscreen.
- A lógica de debug deve ser compartilhada entre desktop e Switch.
- Informações devem permanecer legíveis na tela portátil de 1280 x 720.
- Operações que modificam a emulação devem ser explícitas e seguras.
- Teclado USB e touchscreen podem acelerar tarefas, mas são opcionais.

## Escopo funcional

O port preservará:

- Registradores e estado da CPU.
- Disassembly e indicação da instrução atual.
- Step instruction e avanço de um frame.
- Breakpoints por endereço.
- Watchpoints e histórico de valores.
- Visualização, busca e edição de memória.
- Lista, seleção e visualização de sprites.
- Overlays de sprites e entidades selecionadas sobre o jogo.
- Estado pausado ou em acompanhamento em tempo real.
- Mensagens de resultado e erro das operações.

O escopo vale para GB/GBC desde o MVP. A integração GBA manterá a mesma
experiência onde as capacidades expostas pelo backend mGBA permitirem.

## Layout aprovado

O layout será híbrido adaptativo.

### Dock ou tela com espaço suficiente

O jogo permanece visível à esquerda e o painel de debug aparece à direita. O
painel mostra a aba selecionada, estado da CPU e legenda contextual dos botões.
Qualquer aba pode ser expandida para ocupar a tela inteira.

### Modo portátil

O debugger usa abas em tela cheia para garantir texto legível. O usuário pode
retornar rapidamente ao jogo, que permanece pausado por padrão enquanto o modo
de análise está ativo.

### Abas

1. CPU e disassembly.
2. Memória e edição.
3. Busca de memória.
4. Breakpoints e watchpoints.
5. Sprites e overlays.

Um modo compacto de acompanhamento pode exibir PC, instrução atual e watches
selecionados sobre o jogo durante a execução.

## Estados de execução

Ao abrir o debugger, a emulação pausa automaticamente. A sessão registra se o
jogo já estava pausado antes da abertura. Ao fechar o debugger, o jogo somente
retoma automaticamente se tiver sido pausado pela própria abertura do painel.

No debugger, o usuário pode:

- Executar uma única instrução.
- Avançar um frame.
- Retomar a execução mantendo o painel em modo de acompanhamento.
- Pausar novamente sem perder aba, seleção ou posição de rolagem.

A suspensão do console cancela com segurança qualquer operação de edição ainda
não confirmada.

## Controles aprovados

Fora do debugger, `R3` abre o painel e pausa a emulação.

| Controle | Ação principal no debugger |
|---|---|
| L / R | Alternar abas |
| Direcional | Navegar por linhas, endereços e campos |
| A | Selecionar ou confirmar |
| B | Voltar; na raiz, retornar ao jogo |
| X | Executar uma instrução |
| Y | Adicionar ou remover breakpoint no endereço selecionado |
| ZR | Avançar um frame |
| ZL / ZR | Paginar em listas quando não houver uma ação de execução ativa |
| + | Abrir o menu de ações da aba |
| - | Alternar painel lateral e tela cheia |
| Analógico direito | Rolagem rápida |
| R3 | Fechar o debugger e retornar ao jogo |

Os controles são contextuais e a barra inferior sempre informa as ações
disponíveis. Ações que conflitem, como avanço de frame e paginação com `ZR`,
serão resolvidas pelo foco: controles de execução ficam disponíveis na raiz da
aba; listas e editores consomem os gatilhos enquanto estão em foco.

## Entrada hexadecimal

Um editor hexadecimal próprio permitirá inserir endereços e valores sem
teclado:

- Direcional move o cursor e seleciona dígitos de `0` a `F`.
- `A` confirma o dígito ou a operação.
- `B` apaga ou cancela, conforme o nível atual.
- `L/R` move rapidamente entre posições.
- O valor anterior permanece visível durante uma edição de memória.

Touchscreen e teclado USB poderão alimentar o mesmo modelo de edição, sem criar
um fluxo separado.

## Arquitetura

### DebugController

Recebe ações independentes da plataforma, como:

- `OpenDebugger`
- `CloseDebugger`
- `StepInstruction`
- `StepFrame`
- `ToggleBreakpoint`
- `SetWatchpoint`
- `WriteMemory`
- `ChangeTab`
- `MoveSelection`

Desktop e Switch apenas convertem seus eventos de entrada nessas ações. O
controller valida o estado atual e encaminha operações ao worker de emulação ou
à `DebugSession`.

### DebugSnapshot

Contém uma visão consistente e imutável do estado necessário para renderizar:

- Registradores e flags.
- PC, opcode atual e próxima instrução.
- Linhas de disassembly.
- Leituras, watches e resultados de busca de memória.
- Breakpoints e watchpoints.
- Sprites e seleção atual.
- Estado pausado, execução e mensagens de operação.

O snapshot evita que a interface leia estruturas mutáveis enquanto a emulação
está avançando.

### DebugView

Renderiza o mesmo snapshot em diferentes layouts:

- Painel desktop atual.
- Painel lateral do Switch.
- Abas em tela cheia no portátil.
- Overlay compacto de acompanhamento.

A view não altera diretamente o core e não contém regras de breakpoint,
memória ou execução.

## Fluxo de dados

```text
Joy-Con / Pro Controller
          ↓
ação abstrata de debug
          ↓
DebugController → DebugSession / emulation worker → core
                                                   ↓
                                             DebugSnapshot
                                                   ↓
                                  painel, abas e overlays SDL2
```

## Segurança e tratamento de erros

- Escritas fora das regiões permitidas são recusadas.
- Uma escrita mostra endereço, valor anterior e novo valor antes da confirmação.
- O resultado da escrita permanece visível como mensagem de sucesso ou erro.
- Limites de breakpoints, watchpoints ou resultados exibem uma mensagem clara.
- Operações pendentes são canceladas durante suspensão ou encerramento.
- Falha ao produzir um snapshot mantém o último snapshot válido e informa que
  os dados estão temporariamente desatualizados.
- Fechar o painel respeita o estado de pausa anterior à sua abertura.

## Persistência

Preferências de layout, última aba e opções de acompanhamento podem ser salvas
na configuração global do Switch. Endereços temporários, edições incompletas e
resultados de busca não serão persistidos automaticamente.

No MVP, breakpoints, watchpoints, endereços temporários, edições incompletas e
resultados de busca existem somente durante a sessão e não são restaurados ao
reiniciar o aplicativo.

## Testes

### Testes unitários no host

- Conversão de entradas em ações abstratas.
- Transições entre fechado, análise pausada e acompanhamento.
- Preservação do estado de pausa anterior.
- Step instruction e step frame.
- Adição, remoção e limites de breakpoints/watchpoints.
- Validação e confirmação de escrita de memória.
- Navegação e edição hexadecimal.
- Produção consistente de snapshots.

### Testes de integração

- Debugger conectado ao worker de emulação.
- Snapshot após pausa, step e retomada.
- Busca e edição de memória durante uma sessão real.
- Seleção de sprite e overlay correspondente.
- Troca de abas sem perda de seleção ou rolagem.

### Validação visual e no hardware

- Legibilidade e ausência de cortes em 1280 x 720.
- Layout lateral e expansão em 1920 x 1080.
- Joy-Cons acoplados e Pro Controller.
- Suspensão e retomada do console.
- Operação prolongada no modo de acompanhamento.
- Paridade funcional com o debugger desktop.

## Critérios de aceitação

O debugger do port estará pronto para o MVP quando:

1. Todas as abas puderem ser abertas e operadas somente pelo controle.
2. Step, breakpoints, watchpoints, busca e edição de memória funcionarem em GB e
   GBC.
3. Sprites puderem ser inspecionados e destacados sobre o jogo.
4. Texto e navegação forem utilizáveis em 720p.
5. Abrir e fechar o painel preservar corretamente o estado de pausa.
6. Suspensão e retomada não deixarem escritas ou comandos incompletos.
7. Testes automatizados do controller e dos snapshots passarem no host.
8. A validação no Switch confirmar controles, áudio e vídeo estáveis durante o
   uso do debugger.

## Fora de escopo deste design

- Depuração remota pela rede.
- Integração do RunLab/MCP no Switch.
- Novos recursos de debug que ainda não existem no desktop.
- Alterações no funcionamento interno dos cores de emulação sem relação com a
  interface de debug.
