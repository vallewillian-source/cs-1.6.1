# patches/regamedll-bot-advisor

O módulo `cs_bot_advisor` (o "cérebro lento" dos bots — ver
[`docs/BOTBRAIN.md`](../../docs/BOTBRAIN.md)) vive dentro da árvore do
ReGameDLL. Como `/src/` é ignorado pelo repositório (é um checkout
re-clonável), o módulo e seus ganchos ficam aqui e são reaplicados antes do
build.

| Arquivo | O que é |
|---|---|
| `cs_bot_advisor.{h,cpp}` | o módulo (cópia canônica) |
| `hooks.patch` | as 12 linhas de gancho em 4 arquivos existentes (CMakeLists + `cs_bot_init/manager/update.cpp`) |
| `apply.sh` | copia o módulo e aplica os ganchos; **idempotente** |

```bash
patches/regamedll-bot-advisor/apply.sh          # aplica (ou confirma já aplicado)
patches/regamedll-bot-advisor/apply.sh /caminho/src/ReGameDLL_CS
```

`scripts/build-regamedll.sh` chama o `apply.sh` automaticamente, então um
clone novo de `/src/` volta a compilar sem passos manuais.
