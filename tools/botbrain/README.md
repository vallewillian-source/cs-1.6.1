# botbrain — o "cérebro lento" dos bots (Jev / TypeSafe)

Um sidecar Python que dá **uma decisão de time por vez** aos bots do CS 1.6
(ReGameDLL) no servidor dedicado local. A DLL (`cs_bot_advisor`, dentro do
`cs.so`) escreve os fatos da rodada num arquivo, o botbrain pergunta ao modelo
**Jev** (TypeSafe) qual intent usar, e escreve a resposta de volta. A DLL aplica.

```
servidor (podman, 32-bit)                 host (Python)
┌─────────────────────────────┐          ┌──────────────────────────┐
│ cs.so  CCSBot (o "músculo") │ observe  │ botbrain.py              │
│ cs_bot_advisor ────────────►│─────────►│  1. lê observe_<t|ct>.txt│
│   · escreve observe_*.txt   │          │  2. assinatura → cache?  │
│   · lê plan_*.txt ◄─────────│◄─────────│  3. Jev /v1/systemone    │
│   · aplica nos bots         │  plan    │  4. escreve plan_*.txt   │
└─────────────────────────────┘          │  traces/decisions.jsonl  │
        cstrike/advisor/                  └──────────────────────────┘
```

## Por que assim

- **Bots vivem no servidor** → VR e flat jogam contra os mesmos bots, sem
  nenhum trabalho extra. Nada aqui é client-side.
- **1 request por decisão** — o mesmo contrato do Jev: estado indexado compacto
  entra, uma escolha de um conjunto fechado sai. É barato e cacheável.
- **Nunca bloqueia o jogo**: o transporte é um arquivo. Se este processo morrer,
  os bots continuam jogando (o sidecar não escreve plano; a DLL mantém o que
  tinha, e no início de rodada cai no comportamento stock).

## Rodar

```bash
# tudo junto (sidecar + servidor):
scripts/run-botbrain.sh

# só o sidecar, contra um servidor que já está no ar:
tools/botbrain/run.sh
tools/botbrain/run.sh --once          # processa um tick e sai
tools/botbrain/run.sh --no-model      # não chama o modelo (teste do mailbox/cache)
```

Credenciais: `~/Workspace/jev-ultrafast/.env` (`TYPESAFE_API_KEY`,
`TYPESAFE_MODEL`) e o venv daquele projeto (que já tem `httpx[http2]`). Nada é
copiado nem modificado no clone do jev.

## Protocolo dos arquivos

`cstrike/advisor/observe_<t|ct>.txt` (a DLL escreve, `key=value`):

```
v=1
ts=412.50
team=T
round=7
phase=freeze          # freeze | live | postplant
score_us=3
score_them=4
alive_us=5
alive_them=4
players_us=5
players_them=5
money_avg_us=4100
money_avg_them=900
bomb=none             # none | carried | planted
bomb_zone=none        # A | B | none
history=RUSH_B,STACK_A
```

`cstrike/advisor/plan_<t|ct>.txt` (o sidecar escreve):

```
v=1
seq=2712345           # monotônico: a DLL só aplica seq novo
round=7              # precisa ser a rodada atual, senão é descartado
intent=RUSH_B
source=jev           # jev | cache
```

## Intents (V1, de_dust2, defuse)

T: `RUSH_B`, `RUSH_A_LONG`, `RUSH_A_SHORT`, `SPLIT_A`, `HOLD_AFTER_PLANT`,
`ECO_SAVE`, `DEFAULT`. CT: `STACK_A`, `STACK_B`, `RETAKE_A`, `RETAKE_B`,
`ECO_SAVE`, `DEFAULT`. A lista fechada e as regras do "coach" ficam em
`intents.py`; os nomes têm que existir também em
`src/ReGameDLL_CS/regamedll/dlls/bot/advisor/cs_bot_advisor.h`.

`DEFAULT` = **não sobrescrever** nada (o CCSBot joga sozinho). O mapa
intent→comportamento usa os centros de zona que o próprio mapa fornece
(`TheCSBots()->GetZone(i)->m_center`), então não há coordenadas fixas de dust2
no código.

## Custo — quanto a partida gastou

O trace grava o `usage` que o TypeSafe devolve a cada chamada, e o daemon fecha
cada sessão com um resumo. Para ver o gasto de uma sessão:

```bash
tools/botbrain/run.sh --report                     # tudo que está no trace
tools/botbrain/run.sh --report 2026-10-13T15:52    # só desde esse instante
```

Saída típica:

```
decisões:        159
chamadas ao Jev: 14  (cache: 145)
latência:        média 254 ms · máx 455 ms
tokens:          11193 entrada · 1101 saída
custo (entrada): US$ 0.000470
intents:         ECO_SAVE×111, HOLD_AFTER_PLANT×25, RETAKE_B×23
```

Ao encerrar (Ctrl+C **ou** SIGTERM do `run-botbrain.sh`), o resumo também sai no
fim do log:

```
sessão: decisões=159 chamadas_ao_modelo=14 cache=145 erros=0 | tokens_entrada=11193 tokens_saída=1101
```

Ordem de grandeza, medido: **~800 tokens de entrada e ~80 de saída por chamada**.
O TypeSafe publica **US$ 42 por bilhão de tokens de entrada** (≈ US$ 0,042 por
milhão), o que põe uma partida de minutos na casa de **menos de um centésimo de
dólar**. O preço de **saída não é publicado** no site, então o `custo (entrada)`
do relatório é um piso — o número oficial é o console do TypeSafe.

O que mantém o gasto baixo é o desenho: uma decisão por time por momento
(round start, mudança de fase, bomba plantada, clutch), com cache por
assinatura — numa sessão medida, **14 chamadas cobriram 159 decisões**.

### O que mantém barato

A assinatura da situação é **grossa de propósito** (fase, bomba/zona, clutch,
faixa de economia, placar, último plano), então rodadas parecidas caem no cache
e custam zero. Além disso há um `--min-call-interval` (padrão 2 s por time) para
não disparar rajadas.
