# BOTBRAIN.md — bots com "cérebro lento" (Jev / TypeSafe)

> **Status: V1 (2026-10-13), só de_dust2, cenário defuse.** Roda no servidor
> dedicado local (`server/`), vale igual para VR e flat (bots vivem no
> servidor). Fora do escopo do MVP de VR — é uma trilha separada que usa a
> mesma infraestrutura de servidor.

## 1. A ideia em uma frase

O **CCSBot** do ReGameDLL continua sendo o músculo (nav, visão, mira, armas,
combate); um **sidecar Python** pergunta ao modelo **Jev** (TypeSafe) qual é o
plano *do time* naquela rodada e escreve a resposta num arquivo; a DLL aplica
nos bots. O Jev nunca está no caminho crítico — o jogo não espera por ele.

Por que assim:

- **Bots vivem no servidor** → jogador de VR e de flat enfrentam exatamente os
  mesmos bots; nada muda no cliente.
- **1 request por decisão** é o contrato do Jev (estado compacto entra, uma
  escolha de um conjunto fechado sai). É barato, cacheável e não exige latência
  de tempo real: a decisão cai no freeze ou num evento, não em cada tique.
- **Transporte por arquivo** → zero rede dentro do `cs.so` (32-bit), zero
  threads novas, à prova de crash: sidecar morto = bots seguem no algoritmo.

```
servidor (podman, 32-bit)                    host (Python)
┌──────────────────────────────────┐        ┌───────────────────────────┐
│ cs.so (ReGameDLL)                │observe │ tools/botbrain            │
│  CCSBot  — o "músculo"           │───────►│  botbrain.py              │
│  cs_bot_advisor:                 │        │   · assinatura → cache    │
│   · escreve observe_<t|ct>.txt   │◄───────│   · Jev /v1/systemone     │
│   · lê plan_<t|ct>.txt           │ plan   │   · escreve plan_*.txt    │
│   · aplica a intent nos bots     │        │  traces/decisions.jsonl   │
└──────────────────────────────────┘        └───────────────────────────┘
              cstrike/advisor/
```

## 2. Arquivos

| Arquivo | Papel |
|---|---|
| `src/ReGameDLL_CS/regamedll/dlls/bot/advisor/cs_bot_advisor.{h,cpp}` | o módulo na DLL: cvars, mailbox, detecção de rodada/evento, intent→comportamento |
| `patches/regamedll-bot-advisor/` | **cópia canônica** do módulo + `hooks.patch` (as 12 linhas de gancho) — porque `/src/` é ignorado pelo repo; `apply.sh` reaplica (idempotente) e o `build-regamedll.sh` o chama |
| `tools/botbrain/botbrain.py` | o daemon (mailbox → cache → Jev → plan) |
| `tools/botbrain/typesafe.py` | cliente mínimo do Jev (mesmo protocolo/credenciais do clone `jev-ultrafast`) |
| `tools/botbrain/intents.py` | conjunto fechado de intents + regras do "coach" |
| `tools/botbrain/run.sh` | sobe o daemon com o venv/`.env` do jev-ultrafast |
| `scripts/run-botbrain.sh` | sidecar + servidor juntos |
| `scripts/build-regamedll.sh` | build 32-bit do `cs.so` no container (host sem multilib) |
| `server/cstrike/advisor.cfg` | bots (`bot_quota 8`…) + cvars do advisor |
| `server/cstrike/advisor/` | runtime: `observe_*.txt`, `plan_*.txt`, `plan_cache.json` |

Ganchos no código existente (edições mínimas):

- `CCSBotManager::RestartRound()` → `Advisor_OnRoundStart()`
- `CCSBotManager::StartFrame()` → `Advisor_FrameUpdate()`
- `CCSBot::Update()` (antes do dispatch da máquina de estados) → `Advisor_ApplyIntent(bot)`
- `Bot_RegisterCVars()` → `Advisor_RegisterCVars()`

## 3. Rodar

```bash
scripts/build-regamedll.sh      # compila e instala o cs.so no servidor
scripts/run-botbrain.sh         # sidecar + servidor (dust2)
```

ou separadamente: `tools/botbrain/run.sh` contra um servidor já no ar.

**No VR não precisa de nada disso:** `scripts/run-cs-vr.sh` sobe o servidor de
bots sozinho, espera a porta abrir, entra na partida em VR e derruba tudo na
saída (`CS_BOTS=0` desliga; `CS_BOT_PORT` muda a porta; se já houver servidor na
porta ele só conecta, e um `+connect` seu faz ele nem subir servidor local).

Credenciais: `~/Workspace/jev-ultrafast/.env` (`TYPESAFE_API_KEY`,
`TYPESAFE_MODEL`). Nada é copiado nem modificado no clone do jev.

## 4. Protocolo (arquivos `key=value`)

`cstrike/advisor/observe_<t|ct>.txt` (a DLL escreve, atômico via tmp+rename):

```
v=1  ts=412.50  team=T  round=7  phase=freeze   # freeze | live | postplant
score_us=3  score_them=4  alive_us=5  alive_them=4
players_us=5  players_them=5  money_avg_us=4100  money_avg_them=900
bomb=none        # none | carried | planted
bomb_zone=none   # A | B | none
history=RUSH_B,STACK_A
```

`cstrike/advisor/plan_<t|ct>.txt` (o sidecar escreve):

```
v=1  seq=91915040  round=7  intent=RUSH_B  source=jev   # jev | cache
```

A DLL só aplica se `round` for a rodada atual **e** `seq` for maior que o
último consumido — um plano velho nunca é reaplicado.

## 5. Conjunto fechado de intents (V1, de_dust2/defuse)

| Time | Intents |
|---|---|
| T | `RUSH_B`, `RUSH_A_LONG`, `RUSH_A_SHORT`, `SPLIT_A`, `HOLD_AFTER_PLANT`, `ECO_SAVE`, `DEFAULT` |
| CT | `STACK_A`, `STACK_B`, `RETAKE_A`, `RETAKE_B`, `ECO_SAVE`, `DEFAULT` |

Mapeamento (em `cs_bot_advisor.cpp`), usando só o que o mapa dá:

- `RUSH_*` / `RETAKE_*` → `MoveTo(posição aleatória na zona do site)` +
  `SetTask(SEEK_AND_DESTROY)` + `SetDisposition(ENGAGE_AND_INVESTIGATE)`.
- `STACK_*` / `HOLD_AFTER_PLANT` → `Hide(posição na zona)` +
  `SetTask(GUARD_BOMB_ZONE)` + `SetDisposition(OPPORTUNITY_FIRE)`.
- `ECO_SAVE` → `SetDisposition(SELF_DEFENSE)`.
- `DEFAULT` → **não sobrescreve nada** (o CCSBot joga sozinho).

Não há coordenada de dust2 escrita no código: os alvos saem de
`TheCSBots()->GetZone(i)->m_center` e `GetRandomPositionInZone()`. Qual índice
de zona é o site A ou B vem do **`target` do `func_bomb_target`**
(`tgt_a`/`tgt_b`) — o dust2 nomeia por `target`, não `targetname`.

A intent é aplicada **uma vez por mudança** (a DLL guarda o que já aplicou por
bot); depois disso o CCSBot retoma o controle, inclusive o combate.

## 6. O lado do coach (economia e gatilho)

- **`buy_status`** é calculado no Python, não pedido ao modelo: `<1200` = `eco`,
  `<4000` = `force`, senão `full`. Conhecimento de economia do CS 1.6 fica no
  código, não no prompt.
- **A assinatura é o gatilho**: ela só muda em início de rodada, mudança de
  fase, bomba plantada, time reduzido a clutch (≤2 vivos), virada de placar,
  faixa de economia e último plano. Ou seja, decisões acontecem em **momentos
  significativos**, não a cada morte.
- **Cache** por assinatura: situações repetidas custam zero. Medido em jogo:
  ~400 decisões → ~15–25 chamadas ao modelo.
- **`--min-call-interval`** (2 s por time) evita rajadas.
- **Sem fallback no erro**: se o modelo falhar, nenhum plano é escrito e a DLL
  mantém o que tinha (no início da rodada, o comportamento stock).

### Quanto custa (medido)

O trace grava o `usage` que o TypeSafe devolve, o daemon fecha cada sessão com um
resumo, e `tools/botbrain/run.sh --report [DESDE]` imprime o gasto:

```
chamadas ao Jev: 14  (cache: 145)
tokens:          11193 entrada · 1101 saída
custo (entrada): US$ 0.000470
```

Medido: **~800 tokens de entrada e ~80 de saída por chamada**. O TypeSafe publica
**US$ 42 por bilhão de tokens de entrada** (≈ US$ 0,042 por milhão) — ou seja, uma
partida de minutos custa **menos de um centésimo de dólar**. O preço de **saída
não é publicado**, então o relatório dá um piso de entrada; o número oficial está
no console do TypeSafe.

## 7. Pré-requisitos do servidor (aprendidos na prática)

1. **`bot_enable 1` no `server/cstrike/game_init.cfg`.** Em servidor dedicado
   os bots são gated por `bot_enable`, e o ReGameDLL executa `game_init.cfg`
   **antes** de `Bot_RegisterCVars()` — é a única janela em que dá para setar o
   cvar. Sem isso, nenhum comando `bot_*` existe (o `game_init.cfg` estava
   vazio).
2. **`bot_join_after_player 0`.** Com o default (`1`), sem humano conectado o
   quota desejado é 0 e o servidor **expulsa** os bots ("kicked to maintain
   quota"). Obrigatório para partidas headless.
3. **`BotProfile.db` + `BotChatter.db` no `server/cstrike/`.**
   (`src/cs16-client/3rdparty/cs16client-extras/`).
4. **`server/cstrike/maps/de_dust2.nav`.** Não existe pronto no repo: o nav-mesh
   é gerado pelo próprio bot. Com o mapa carregado e um bot vivo, rode
   `bot_nav_analyze` e depois de ~90 s `bot_nav_save` (o arquivo tem ~420 KB e
   passa a carregar sozinho).
5. **`exec advisor.cfg` no `server.cfg`.** `+exec` na linha de comando não
   serve: roda antes de o game DLL registrar os cvars `bot_*`.

## 8. Depurar

Cvars do módulo: `advisor_enable` (0 = stock byte-a-byte), `advisor_debug`
(1 = plano/rodada/aplicação; 2 = também o objetivo de cada bot),
`advisor_force_intent <NOME>` (dirige a metade DLL sem o sidecar),
`advisor_poll_interval`, `advisor_heartbeat`.

```bash
grep ADVISOR server/...            # ou o stdout do servidor
cat tools/botbrain/traces/decisions.jsonl | tail   # cada decisão + contexto
tools/botbrain/run.sh --report                     # quanto a sessão gastou
tools/botbrain/run.sh --once --no-model --dir server/cstrike/advisor   # testa o mailbox sem gastar
```

O módulo também loga, no início de cada rodada com debug ligado, os centros de
zona e o site (`ADVISOR: zone 0 (B) target='tgt_b' …`).

## 9. O que ficou provado na V1

- Bots jogam dust2 de verdade (nav gerado, 4v4, plantam e defusam).
- `RUSH_B` → bomba plantada no **site B**; `RUSH_A_LONG` → **site A**
  (contagem por amostragem de `bomb_zone`).
- Laço completo em jogo: observe → Jev → plan → aplicado nos bots
  (`ADVISOR: team CT <- RETAKE_B`, `applied RETAKE_B` ×…).
- Sensibilidade da decisão (observações forjadas): full/even → `RUSH_B`;
  eco/losing → `ECO_SAVE`; postplant em B → `HOLD_AFTER_PLANT`; postplant em A
  (CT) → `RETAKE_A`; **inimigo repetindo B → `STACK_A`**.
- Custo: ~25 chamadas ao modelo em ~2 min de partida (o resto vem do cache).

## 10. Limites da V1 (deliberados)

- Só `de_dust2`; só o cenário defuse.
- Uma decisão por **time** por momento — não há Jev por bot.
- Sem política aprendida/destilação, sem chatter gerado, sem micro-gestão de
  compra, sem transporte por socket, sem UI.
- A variedade de intents depende da economia da partida: com times pobres, o
  Jev tende a `ECO_SAVE` — o que é correto, mas faz o lado pobre atacar menos.
