# Changelog

## Refatoracao completa do runtime e frontend

- mGBA nativo passou a ser o unico backend GBA suportado pelo executavel.
- O core GBA proprio foi isolado no alvo opcional `gbgba_experimental` e fica
  desabilitado por padrao.
- O carregador Libretro e o replay incompleto foram removidos.
- O frontend SDL passou a usar opcoes de sessao, persistencia, RunLab, netplay,
  depuracao, workers e recursos SDL encapsulados.
- Os modulos comuns agora sao compilados uma vez em `gbfrontend_support`.
- A matriz de build cobre runtime nativo, GB/GBC-only e core experimental.
- O CTest deixou de executar repetidamente o mesmo binario por suite.

O historico detalhado de commits continua disponivel no Git.
