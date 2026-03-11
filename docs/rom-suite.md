# ROM Compatibility Suite

O emulador agora suporta execucao de suite de ROMs em modo headless:

```bash
gbemu --rom-suite roms/tests/manifest.txt
```

## Formato do manifesto

Cada linha valida tem este formato:

```text
nome_do_teste|caminho/da/rom.gb|frames|chave=valor|chave=valor|...
```

- Linhas vazias e linhas com `#` no inicio sao ignoradas.
- `caminho/da/rom.gb` e relativo ao diretorio do manifesto.
- `frames` define quantos frames rodar antes de validar.

## Chaves suportadas

- `title` (string)
- `pc`, `sp` (0x0000..0xFFFF)
- `a`, `b`, `c`, `d`, `e`, `h`, `l`, `f`, `ie`, `if` (0x00..0xFF)
- `halted` (`0/1`, `true/false`, `on/off`)

## Exemplo

```text
# nome|rom|frames|expectativas
blargg_cpu_01|blargg/cpu_instrs.gb|240|pc=0x0180|a=0x00
mooneye_timer|mooneye/timer/tima_reload.gb|300|pc=0x40|halted=1
```

Saida por caso:

- `[PASS] <nome>`
- `[FAIL] <nome> -> <motivo>`

Resumo final:

- `[ROM-SUITE] total=N pass=P fail=F`

