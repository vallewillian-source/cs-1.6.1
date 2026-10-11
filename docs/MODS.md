# MODS.md — Arquitetura de Mods do CS 1.6 open-source

> **Status: RASCUNHO (Fase 0 do `PLAN.md`).** Este documento registra a *decisão de
> arquitetura* e o desenho do contrato. A ABI v1 será congelada na tarefa 1.1
> (Fase 1), junto com o header `common/mod_api.h` e o loader no engine.

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

## 3. Contrato previsto (v1 — congelar na Fase 1)

```c
// common/mod_api.h — RASCUNHO. NÃO É A ABI FINAL.
#define MOD_API_VERSION 1

typedef enum {
    MOD_CAP_DISPLAY = 1 << 0,   // quer ser "display owner" (câmera estéreo + submit)
    MOD_CAP_INPUT   = 1 << 1,   // injeta input (movimento, botões, ângulos)
    MOD_CAP_HUD     = 1 << 2,   // redireciona/captura desenho 2D (HUD, menus)
} mod_capability_t;

typedef struct mod_api_s {
    int             api_version;      // MOD_API_VERSION
    const char     *mod_name;
    unsigned int    capabilities;     // bitmask de mod_capability_t

    // lifecycle
    int  (*Mod_Init)( struct mod_engine_api_s *eng );  // troca de APIs acontece aqui
    void (*Mod_Shutdown)( void );

    // frame
    void (*Mod_FramePre)( double time );   // pacing VR, coleta de poses
    void (*Mod_FramePost)( double time );

    // capability: display
    void (*Mod_Camera)( int eye, struct mod_view_s *view ); // origem/ângulos/projeção por olho
    int  (*Mod_BeginFrame)( struct mod_gl_ctx_s *ctx );     // bind do FBO do olho
    void (*Mod_EndFrame)( void );                           // submit (xrEndFrame)

    // capability: input
    void (*Mod_Input)( struct mod_input_sample_s *sample ); // move/buttons/angles por frame
    void (*Mod_HudPointer)( float x, float y, int click );  // ponteiro → coords 2D

    // capability: hud
    int  (*Mod_HudRedirect)( int enable );                  // captura desenho 2D p/ canvas
} mod_api_t;

// único símbolo exigido do .so:
mod_api_t *ModGetAPI( void );
```

### Pontos de hook no engine (call sites neutros)

1. Início do frame do host → `Mod_FramePre`
2. Antes do setup de câmera do renderer (uma chamada por olho se `MOD_CAP_DISPLAY`) → `Mod_Camera`, `Mod_BeginFrame`
3. Pós-desenho de HUD/menus (desenho 2D) → `Mod_HudRedirect` / `Mod_FramePost`
4. Fila de input do engine → `Mod_Input` (mesma rota do teclado/mouse — client prediction intacta)
5. Fim do frame → `Mod_EndFrame`
6. Shutdown do host → `Mod_Shutdown`

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
| `vr` (OpenXR, WiVRn/Quest 3) | display + input + hud | **este projeto** — Fases 2–4 do `PLAN.md` |
| `null` (template log-only) | nenhuma (só lifecycle + frame) | Fase 1 — exemplo oficial e teste da arquitetura |
| *futuro:* arma 6DoF, HD pack, gameplay, acessibilidade | — | pós-MVP, sobre a mesma ABI |

## 6. Referências

- `PLAN.md` na raiz do workspace — plano completo, fases e critérios de aceite.
- Base de engine: `src/xash3d-fwgs` (branch `vr-mod`) · client: `src/cs16-client`
  (branch `vr-mod`) · baseline dos binários flat: `backups/flat-baseline-20261011/`.
