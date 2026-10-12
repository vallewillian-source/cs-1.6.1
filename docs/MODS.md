# MODS.md — Arquitetura de Mods do CS 1.6 open-source

> **Status: ABI v2 (Fase 2 do `PLAN.md`, 2026-10-11).**
> v1 (Fase 1) congelada; **v2 acrescentou, no FIM das structs**: `mod_view_t.viewport[4]`,
> `Mod_EyeCount()` e `Mod_AfterEyes()` (estéreo — passes de render por frame e hook
> pré-2D). Fonte da verdade: `src/xash3d-fwgs/engine/common/mod_api.h` (branch `vr-mod`).
> Loader: `src/xash3d-fwgs/engine/common/modloader.c`. Mods: `mods/null/` (template) e
> `mods/vr/` (OpenXR — o primeiro Mod real).
>
> Gotcha de build C++: `ModGetAPI` precisa de `extern "C"` (mangling), e a linkagem do
> mod precisa do fonte ANTES das `-l` (Ubuntu usa `--as-needed` por padrão).

## 1. O que é um "Mod" aqui

Um **Mod** é uma biblioteca compartilhada (`mod_<nome>.so`) que o engine
(Xash3D FWGS, branch `vr-mod`) carrega em tempo de execução e que estende o
jogo através de **hooks** em pontos de extensão definidos.

Não é um "game dir" do GoldSrc/Xash (`-game cstrike` continua existindo e
continua sendo a fonte de conteúdo). Um Mod **não duplica conteúdo** — ele é
código. O primeiro Mod é o **`vr`** (realidade virtual via OpenXR).

## 2. Decisões de arquitetura (registradas em 2026-10-11, Fase 0)

| # | Decisão | Justificativa |
|---|---------|---------------|
| 1 | **C ABI, struct versionada**, um único símbolo exportado (`ModGetAPI`) | estabilidade binária: engine e Mod compilam separadamente, até com compiladores diferentes; campos novos no fim + `api_version` = compatibilidade nos dois sentidos |
| 2 | **Carregamento via `dlopen`**, acionado por `-mod <nome>` (engine procura `mod_<nome>.so`) | zero custo quando desligado; múltiplos Mods possíveis no futuro sem colisão; unload limpo no shutdown |
| 3 | **Capabilities declaradas** (bitmask: `display`, `input`, `hud`) | o engine pergunta, o Mod responde: só são chamados os hooks do que o Mod declara; permite conviver Mods de naturezas diferentes |
| 4 | **Hook não registrado = comportamento padrão do engine** | fallback total: sem `-mod`, o jogo é byte-a-byte o atual |
| 5 | **O engine nunca conhece OpenXR** (nem qualquer tecnologia de Mod específica) | o conhecimento de VR vive 100% no `mod_vr.so`; o engine só conhece o contrato genérico |
| 6 | **VR é client-side** — servidor (ReHLDS/ReGameDLL), protocolo e conteúdo intocados | cliente VR e cliente flat jogam no mesmo servidor, indistinguíveis |

## 3. Contrato v1 (congelado — ver `engine/common/mod_api.h`)

A struct completa está no header (fonte da verdade). Resumo do que o Mod implementa/expõe:

- **Exportação obrigatória**: `mod_api_t *ModGetAPI( void )` — único símbolo exigido.
- **Identidade**: `api_version` (tem que ser exatamente `MOD_API_VERSION`=1), `mod_name`, `capabilities` (`MOD_CAP_DISPLAY`, `MOD_CAP_INPUT`, `MOD_CAP_HUD`).
- **Lifecycle**: `Mod_Init( mod_engine_api_t* )` (recebe `Printf`, `DoubleTime`, `engine_version`; retorno 0 recusa o load) e `Mod_Shutdown()`.
- **Frame** (sempre chamados): `Mod_FramePre( time )` / `Mod_FramePost( time )`.
- **`MOD_CAP_DISPLAY`**: `Mod_Camera( mod_view_t* )` — por view pass, pode sobrescrever origem/ângulos/FOV (projeção externa fica para a fase 2); `Mod_BeginFrame( mod_gl_ctx_t* )` (janela + contexto GL do engine); `Mod_EndFrame()`.
- **`MOD_CAP_INPUT`**: `Mod_Input( mod_input_sample_t* )` — 1x/frame. **No v1 o engine entrega a amostra e não aplica o conteúdo** (aplicação real, pela rota do teclado/mouse com prediction, chega na Fase 3 com o mod_vr).
- **`MOD_CAP_HUD`**: `Mod_HudRedirect( enable )` (pré-desenho 2D) e `Mod_HudPointer( x, y, buttons )` (ponteiro laser → 2D; wire-up na Fase 4).

### Call sites no engine (implementados na Fase 1)

| Call site | Arquivo | Hook |
|---|---|---|
| `Host_Init` (após exec dos configs) | `engine/common/host.c` | `Mods_Init` — parse `-mod <nome>` + load |
| `Host_Frame` | `engine/common/host.c` | `Mods_FramePre` → `Host_InputFrame` → `Mods_Input` → … → `Mods_FramePost` |
| `Host_ShutdownWithReason` | `engine/common/host.c` | `Mods_Shutdown` (unload limpo) |
| `SCR_UpdateScreen` | `engine/client/cl_scrn.c` | `Mods_BeginFrame` (pré-3D), `Mods_HudFrame` (pré-2D), `Mods_EndFrame` (pós-render) |
| `V_RenderView` (por view pass) | `engine/client/cl_view.c` | `Mods_Camera( &rvp, viewnum )` — pode sobrescrever a view |
| Contexto GL pronto | `engine/platform/sdl2/vid_sdl2.c` | `Mods_VideoInit( window, ctx )` |

### Carga e busca

- Linha de comando: `./xash3d -game cstrike -mod null`.
- Caminhos testados, em ordem: `<rootdir>/mods/mod_<nome>.so`, `<rootdir>/mod_<nome>.so` (rootdir = pasta do `xash3d`, ex.: `game/`).
- Console: `mod_info` mostra mod carregado, capabilities e contadores de hooks.
- Erros (arquivo ausente, símbolo ausente, `api_version` errado, `Mod_Init` recusando) recusam o load **sem derrubar o jogo** — o engine segue sem mod.

## 4. Regras para autores de Mod

1. Todo acesso ao engine passa pelo `mod_engine_api` recebido em `Mod_Init` —
   nada de linking direto contra símbolos do engine.
2. Nunca bloqueie: hooks rodam no frame do jogo; trabalho pesado vai pra thread própria.
3. Respeite `capabilities`: o engine só chama o que foi declarado.
4. Um Mod não pode assumir que outro Mod existe.
5. Erro de versão (`api_version` diferente) = recusa de load com mensagem clara.

## 5. Mods planejados

| Mod | Capabilities | Status |
|-----|--------------|--------|
| `vr` (OpenXR, WiVRn/Quest 3) | display | **em construção** — estéreo da Fase 2 feita (sem headset o init roda em thread com timeout e o jogo segue flat); input/hud nas Fases 3/4 |
| `null` (template log-only) | nenhuma (só lifecycle + frame) | **feito (Fase 1)** — `mods/null/`, `game/mods/mod_null.so`; também é o teste de gating do engine |
| *futuro:* arma 6DoF, HD pack, gameplay, acessibilidade | — | pós-MVP, sobre a mesma ABI |

## 6. Como escrever um Mod (tutorial rápido)

1. Copie `mods/null/` para `mods/<seumod>/` e troque o nome em `mod_api.mod_name`.
2. Declares as capabilities que vai usar; implemente os hooks correspondentes.
3. Build: um `gcc -O2 -fPIC -shared -I<engine>/engine/common -o mod_<nome>.so <fonte>` basta
   (veja `mods/null/build.sh` — o único include é `mod_api.h`, autocontido).
4. Instale em `game/mods/mod_<nome>.so` e rode com `-mod <nome>`.
5. Regras da seção 4 valem; erros de load não derrubam o jogo.

## 7. Referências

- `PLAN.md` na raiz do workspace — plano completo, fases e critérios de aceite.
- Base de engine: `src/xash3d-fwgs` (branch `vr-mod`) · client: `src/cs16-client`
  (branch `vr-mod`) · baseline dos binários flat: `backups/flat-baseline-20261011/`.
