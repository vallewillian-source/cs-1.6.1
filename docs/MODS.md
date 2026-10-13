# MODS.md — Arquitetura de Mods do CS 1.6 open-source

> **Status: ABI v8 (2026-10-12).**
> v1 (Fase 1) congelada; **v2** acrescentou, no FIM das structs: `mod_view_t.viewport[4]`,
> `Mod_EyeCount()` e `Mod_AfterEyes()` (estéreo — passes de render por frame e hook
> pré-2D). **v3** acrescentou `Mod_Overlay2D()`: o último ponto de desenho do frame
> (HUD, menus, console e placar prontos; swap ainda não feito), usado pelo painel de
> debug da tela 2D do `mod_vr`. **v4** acrescentou `mod_view_t.fov_asym[4]`/`has_fov_asym`
> + a flag `RF_FOV_ASYM` no `ref_viewpass_t`: frustum **fora de centro por olho** — o
> que a ótica canted do headset exige (ver §"Frustum por olho" abaixo). **v5** (Fase 3)
> fez o `MOD_CAP_INPUT` sair da inércia: `mod_input_sample_t` ganhou `move_up`,
> `viewangles_abs[3]` e `has_viewangles_abs`, e o engine passou a **APLICAR** a amostra
> no `usercmd` (ver §"Input" abaixo). **v6** acrescentou `Mod_ViewModel` +
> `mod_viewmodel_t`: o Mod leva a **arma para a mão** (sobrescreve origem/ângulos do
> viewmodel) e escolhe o depth — `RF_VIEWMODEL_WORLD` (a arma passa a ser **ocluída
> pelo mundo**) e `RF_NO_VIEWMODEL` (esconder; para um Mod que desenhe a própria
> arma) — ver §"Arma na mão" abaixo. **v7** acrescentou `Mod_Key` + `MOD_CAP_KEYS`:
> o Mod vê o evento de teclado **antes de todo handler do engine** (OSK, console,
> menu, binds e cliente DLL) e pode **consumi-lo** — é o que permite uma UI de
> calibração por teclado (o ajuste fino do IPD do `mod_vr` usa isso) sem a tecla
> vazar para o jogo. **v8** trocou os dois stubs nunca fiados do `MOD_CAP_HUD`
> (`Mod_HudRedirect`/`Mod_HudPointer`) por **um intercâmbio por frame** —
> `Mod_Hud( mod_hud_t* )`: o engine entrega os **fatos** (menu aberto, tamanho do
> espaço 2D, textura do canvas) e o Mod decide a **política** (quer o 2D num canvas
> offscreen? onde está o cursor do menu e quando clicar?). Junto vieram o **canvas
> 2D** no ref, o **cursor virtual** que os dois consumidores de cursor do engine
> leem, e o `R_InvalidateTextures` (ver §"Menus no headset"). Fonte da
> verdade: `src/xash3d-fwgs/engine/common/mod_api.h` (branch `vr-mod`). Loader:
> `src/xash3d-fwgs/engine/common/modloader.c`. Mods:
> `mods/null/` (template) e `mods/vr/` (OpenXR — o primeiro Mod real).
>
> **Mod antigo não carrega em engine novo (e vice-versa):** o loader exige
> `api_version == MOD_API_VERSION` exato — como a struct cresceu, um mod compilado
> contra a v3 leria campos deslocados. Ao subir a ABI, recompile `mods/*` (o
> `mods/null` é o primeiro a acusar).
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
- **Identidade**: `api_version` (tem que ser exatamente `MOD_API_VERSION`), `mod_name`, `capabilities` (`MOD_CAP_DISPLAY`, `MOD_CAP_INPUT`, `MOD_CAP_HUD`, `MOD_CAP_KEYS`).
- **Lifecycle**: `Mod_Init( mod_engine_api_t* )` (recebe `Printf`, `DoubleTime`, `engine_version`; retorno 0 recusa o load) e `Mod_Shutdown()`.
- **Frame** (sempre chamados): `Mod_FramePre( time )` / `Mod_FramePost( time )`.
- **`MOD_CAP_DISPLAY`**: `Mod_Camera( mod_view_t* )` — por view pass, pode sobrescrever origem/ângulos/FOV (projeção externa fica para a fase 2); `Mod_BeginFrame( mod_gl_ctx_t* )` (janela + contexto GL do engine); `Mod_EndFrame()`.
- **`MOD_CAP_INPUT`**: `Mod_Input( mod_input_sample_t* )` — 1x/frame. **No v1 o engine entrega a amostra e não aplica o conteúdo** (aplicação real, pela rota do teclado/mouse com prediction, chega na Fase 3 com o mod_vr).
- **`MOD_CAP_HUD`**: `Mod_Hud( mod_hud_t* )` (v8 — fatos do engine + decisões do Mod por frame; ver §"Menus no headset"), `Mod_HudRedirect`/`Mod_HudPointer` (os stubs sem fiamento da v1 **saíram** na v8) e, na v3, `Mod_Overlay2D()`.
- **`MOD_CAP_KEYS`** (v7): `Mod_Key( key, down )` — evento de teclado antes de todo handler; retorno 1 = consumido (ver §"Teclado" abaixo).

### `Mod_Overlay2D()` (v3, `MOD_CAP_HUD`)

Último ponto de desenho do frame: é chamado no fim do `V_PostRender`, **depois** de
HUD, menus, console, placar e do `SCR_MakeScreenShot`, e **antes** do `R_EndFrame`
(que apresenta o render target e faz o swap). Consequências práticas:

- o que o Mod desenha aqui fica **por cima** de toda a UI do jogo;
- o alvo é o framebuffer 2D da janela; no VR os olhos vivem em outras swapchains
  (submetidas ao runtime no `Mod_EndFrame`), então um overlay aqui **não vai para o
  headset** — só para o monitor;
- não entra em screenshots do jogo (o `SCR_MakeScreenShot` roda antes).

Use `glGetIntegerv( GL_VIEWPORT )` para saber o espaço de coordenadas: o
`R_Set2DMode` já fixou o viewport no alvo 2D, então o overlay fica correto com ou sem
o render target interno do `ref_gl` (ver `r_scene_scale`/`r_msaa`).

**Armadilha do alvo (2026-10-12):** com o VR ativo o **binding herdado não é a
janela** — o frame termina com o FBO da imagem do **último olho** ligado (o blit do
espelho salva e devolve o binding que achou). Quem desenha 2D sem dizer onde
desenha acaba escrevendo **dentro da imagem que vai para o headset**. Foi assim que
o painel de debug e o HUD do jogo apareceram no HMD e não no monitor. Duas
correções:

- `ref/gl/gl_fbo.c` (`GL_BindScreenTarget`, chamado por `R_Set2DMode(true)`): sem
  render target interno o 2D desenha **na janela**, e agora isso é dito
  explicitamente (`glBindFramebuffer( GL_FRAMEBUFFER, 0 )`) em vez de confiar no
  binding herdado;
- o `mod_vr` também **liga a janela por conta própria** antes de pintar o painel
  (e devolve o binding depois), para não depender do engine nem da ordem dos
  hooks.

Regra para qualquer Mod: antes de desenhar em 2D, **binde o alvo explicitamente**
(`GL_DRAW_FRAMEBUFFER_BINDING`, e salve/restaure se não for dono do frame).

Quem consome hoje: o **painel de debug da tela 2D** do `mod_vr` (FPS + erros/avisos do
carregamento do VR; `VR_DEBUG_PANEL`, `VR_PANEL_DUMP` — detalhes no cabeçalho de
`mods/vr/mod_vr.cpp`).

### `mod_view_t.fov_asym[4]` / `has_fov_asym` (v4, `MOD_CAP_DISPLAY`)

`fov_x`/`fov_y` **só expressam frustum centralizado**: o renderer monta
`xMax = zNear·tan(fov_x/2)`, `xMin = -xMax` (idem vertical). Headset de ótica
**canted** não entrega isso — o runtime declara, por olho, quatro ângulos
independentes e espelhados. Medido no Quest 3 via WiVRn:

```
olho esquerdo:  L 54,0°  R 40,0°  U 44,0°  D 55,0°
olho direito:   L 40,0°  R 54,0°  U 44,0°  D 55°
```

São 94° horizontais e 99° verticais **fora de centro** (assimetria de ±14° na
horizontal e 11° na vertical, com sinal trocado entre os olhos). Desenhar um
frustum centrado de 54°×55° e submeter o assimétrico faz o compositor esticar a
imagem de cada olho por um fator diferente — o sintoma clássico é "cada olho
sozinho parece bom, os dois juntos não fundem".

Com `has_fov_asym = 1` o Mod entrega os quatro ângulos em **radianos e no sinal do
OpenXR** (`angleLeft`/`angleDown` negativos) e o `ref_gl` monta a projeção
off-center em `R_SetupProjectionMatrix` (`ref/gl/gl_rmain.c`); a flag
`RF_FOV_ASYM` viaja no `ref_viewpass_t` (`common/ref_params.h`).

**O culling também usa os quatro ângulos** (`GL_FrustumInitProjAsym`, chamada em
`R_SetupFrustum` quando a flag está setada): antes ele era montado de
`fov_x`/`fov_y`, que só descrevem um retângulo centrado — e um frustum
assimétrico só se deixa cobrir por um envelope, que sobra no lado largo e **falta
no estreito**. O que falta não é submetido a desenho: a geometria que vive só em
cima daquela faixa (paredes distantes, o céu, água, partículas) desaparece da
imagem, e o que aparece no lugar é o que estava atrás — o mapa "renderiza
diferente" na periferia. Como `R_CullSurface` só descarta superfície cuja **caixa
inteira** cai fora, superfícies grandes que atravessam o centro continuam sendo
desenhadas por inteiro: era isso que fazia o defeito parecer "90% certo".

`fov_x`/`fov_y` seguem obrigatórios, mas atenção à **unidade**: são aberturas
**totais** em graus (o engine divide por dois internamente — `tan(fov_x/2)` na
projeção, `SinCos(fov*0,5)` no frustum antigo). Publicar um meio-ângulo ali vale
como se fosse o dobro e encolhe o culling à metade da abertura — foi exatamente o
bug que este caminho assimétrico veio resolver; com ele, o valor de `fov_x`/`fov_y`
deixou de importar para o culling (mas mantenha-o correto: `right − left` e
`up − down` para qualquer outro consumidor).

### Input: `mod_input_sample_t` aplicado ao `usercmd` (v5, `MOD_CAP_INPUT`)

Na v1 o hook existia mas era inerte: `Mods_Input` entregava o struct zerado e
**descartava** a resposta (comentário antigo em `modloader.c`). A v5 fechou o
caminho, em duas metades:

1. **Poll** — `Mods_Input()` continua sendo chamado 1x/frame em `Host_Frame`
   (`host.c`, logo depois de `Host_InputFrame`) e agora **guarda** a amostra;
   `Mods_GetInput( &sample )` a entrega a quem for aplicar.
2. **Aplicação** — `IN_ModAppendMove( cmd, active )` (`engine/client/input/input.c`)
   é chamada no `CL_CreateCmd` (`cl_main.c`), **imediatamente depois** do
   `CL_CreateMove` do cliente e da `IN_EngineAppendMove`, e antes do
   `CL_PredictMovement`.

Por que **depois** do client DLL: é o DLL quem lê mouse/teclado e escreve
ângulos e `cmd->buttons` (por atribuição, não OR). Aplicar depois faz o Mod
**vencer o mouse** sem desligá-lo — o mouse continua navegando menus, porque
tanto a `IN_EngineAppendMove` quanto a `IN_ModAppendMove` cortam em
`cls.key_dest != key_game`. E como o `CL_PredictMovement` vem em seguida, o que
o Mod escreve é o que vai no `usercmd` **enviado ao servidor** e na predição.

Campos que a v5 acrescentou (no fim do struct, como sempre):

| Campo | Para que |
|---|---|
| `move_up` | eixo vertical → `cmd->upmove` (no chão o pmove zera `wishvel[2]`, então só vale onde o jogo já usa: água, espectador) |
| `viewangles_abs[3]` | orientação **absoluta** (pitch, yaw, roll) |
| `has_viewangles_abs` | `1` = usar o absoluto; `0` = vale o `viewangles_delta` do v1 |

**Absoluto, não delta.** A pose do HMD já é uma orientação absoluta; com delta o
Mod brigaria com o mouse e com o `CL_AdjustAngles` do cliente. Absoluto também é
o que torna o "movimento orientado pela cabeça" gratuito: o Mod injeta o yaw do
HMD e manda o stick **cru** em `forwardmove`/`sidemove` — o `pm_shared` monta o
`wishdir` com `AngleVectors(cmd.viewangles)` e ignora o pitch no chão, logo
"olhar para cima" não muda a velocidade e "virar a cabeça" muda a direção de
andar. Nenhuma matemática de direção no Mod.

Regras de aplicação (em `IN_ModAppendMove`):

- Ângulos: copiados para `cmd->viewangles` **e** para `cl.viewangles` (igual à
  `IN_EngineAppendMove`, para predição e view model acompanharem). Pitch é
  travado em ±89 como no `CL_CreateMove` do cliente — o render pode olhar mais
  longe (`Mod_Camera` usa a pose crua), mas o que vai ao servidor fica dentro do
  que o jogo aceita.
- Movimento: só é **assumido** quando o stick saiu da deadzone (`valor != 0`).
  Com o stick centrado o que veio do teclado permanece — teclado segue como
  fallback em qualquer eixo.
- `MOD_BTN_WALK` aplica `cl_movespeedkey` (mesmo fator do `+speed` do CS).
- Botões → bits de `usercmd` do protocolo GoldSrc, que **já existem** em
  `common/const.h`: `MOD_BTN_ATTACK→IN_ATTACK`, `ATTACK2→IN_ATTACK2`,
  `JUMP→IN_JUMP`, `USE→IN_USE`, `RELOAD→IN_RELOAD`, `CROUCH→IN_DUCK`,
  `SCORE→IN_SCORE`. `MOD_BTN_MENU` não tem bit equivalente — quem trata é o Mod.

### Arma na mão: `Mod_ViewModel` (v6, `MOD_CAP_DISPLAY`)

Até a v5 o viewmodel era intocável: o client DLL escrevia origem e ângulos em
`clgame.viewent` (a **orientação** vem do `V_CalcGunAngle`, derivada da view; a
**origem** é o olho do player, com bob/smoothing) e o ref só desenhava — não havia
hook nenhum no meio. A v6 abre o buraco com uma struct **de entrada e saída**: o
engine pré-preenche com o que o DLL montou e o Mod decide o que muda.

| Campo | Sentido | Para que |
|---|---|---|
| `eye` | entrada | olho do pass corrente |
| `player_origin[3]` | entrada | olho **central** do player no mundo — sem o offset de roomscale e **sem o afastamento entre os olhos** |
| `origin[3]`, `angles[3]` | entrada/saída | o que o client DLL escreveu; o Mod sobrescreve e marca |
| `model_name[64]` | entrada | ex.: `v_ak47.mdl` — deixa o Mod afinar o encaixe por classe de arma |
| `has_origin`, `has_angles` | saída | aplicar `origin[]` / `angles[]` (os ângulos espelham em `curstate.angles` e `latched.prevangles`, como o DLL faz) |
| `world_depth` | saída | depth **normal**: a arma é ocluída pelo mundo |
| `hide` | saída | não desenhar o viewmodel (o Mod desenha o dele) |

Três decisões que não são óbvias:

1. **Chamado DEPOIS do `Mods_Camera` e por olho.** Depois porque o `V_GetRefParams`
   **zera** `rvp->flags` (`cl_view.c`) — uma flag pedida antes dele seria apagada.
   Por olho porque o `pfnCalcRefdef` roda de novo a cada pass e reescreve o
   viewmodel: sem reaplicar, no segundo olho a arma voltaria para o meio da cabeça.
   Consequência de contrato: o Mod precisa ser **idempotente dentro do frame** (só
   ler estado).
2. **`player_origin` é o olho central, não o `vieworigin` do `rvp`.** O vieworigin já
   saiu do `Mods_Camera` com o offset de roomscale **e** com o afastamento do olho. A
   arma é um objeto único no mundo e os dois olhos têm de vê-la no mesmo lugar: usar
   o vieworigin daria ~1,3 cm de disparidade por olho e a arma apareceria com
   profundidade errada.
3. **`RF_VIEWMODEL_WORLD` desliga um hack, não acrescenta um recurso.** O
   `R_DrawViewModel` comprime o depth range do viewmodel (`VIEWMODEL_DEPTH_RANGE`)
   justamente para a arma **nunca** cortar na parede — comportamento certo num
   monitor e errado em VR, onde a arma tem de desaparecer atrás da geometria. Sem
   pedir a flag, **nada muda** para nenhum Mod.

O painel de debug continua sendo 2D na janela: nada aqui o empurra para o headset —
quem trata menus/HUD no HMD é `Mod_Hud` + o canvas 2D (v8, §"Menus no headset").

### Teclado: `Mod_Key` (v7, `MOD_CAP_KEYS`)

Chamado no **topo** do `Key_Event` do engine (`engine/client/input/in_keys.c`),
logo depois do `Key_Rotate` e antes de tudo: teclado virtual, console, menu,
binds e o `pfnKey_Event` do cliente DLL.

```c
int (*Mod_Key)( int key, int down );   // 1 = consumido
```

- **`key`** é o keynum do engine (`engine/keydefs.h`): teclas imprimíveis chegam
  como **ASCII minúsculo** (`'a'`=97, `','`=44) e as especiais como constantes
  (`K_F9`=143, `K_HOME`=151…). Como o `mod_api.h` é autocontido de propósito, as
  especiais que uma UI de debug costuma querer estão fixadas nele como
  `MOD_KEY_*` — e o mapeamento é por **scancode**, não por layout: `'['`/`']'`
  são as teclas onde o layout americano as tem (no ABNT2 ficam trocadas).
- **Retorno 1 = consumido**: o `Key_Event` encerra ali e a tecla **não chega ao
  jogo** (nem aos binds). Devolver 0 segue o caminho normal.
- **Consumir só o que se usa.** Um Mod deve devolver 0 quando a UI dele está
  desligada — consumir sempre apagaria a tecla do jogo sem o usuário pedir nada.
  Quem quiser preservar o *down* e o *up* coerentes (para o jogo não ver meia
  tecla) consome os dois eventos.

Uso de referência: o ajuste fino do IPD do `mod_vr` (`VR_IPD`/`mods/vr_ipd.cfg`)
liga o modo com F9/Home/End e, enquanto ele está ligado, consome `-`, `=`, `,`,
`.` e `0`. Fora do modo, nenhuma tecla é consumida.

### Menus no headset: `Mod_Hud` + o canvas 2D (v8, `MOD_CAP_HUD`)

**O problema:** o menu do jogo (pausa, time, classe, compra) é desenhado pelo
`mainui`/client DLL com as funções **2D do engine**, direto no framebuffer da
janela. Para ele existir no headset não dá para "reimplementar o menu": é preciso
**capturar esse desenho** e mostrá-lo lá. E para o ponteiro funcionar, o caminho
mais curto é o inverso: deixar o engine **crer** que o mouse está onde o laser
aponta.

**Três peças, cada uma num lugar diferente:**

1. **Fatos** (`Mod_Hud`, 1x/frame): o engine entrega `menu_open` (`cls.key_dest ==
   key_menu` — o menu do ESC e os menus do client, time/classe/compra, **todos**
   marcam `KEY_MENU` ao abrir), `ui_visible` (`UI_IsVisible()`), o tamanho do
   espaço 2D e a textura do canvas; o Mod devolve `want_canvas`,
   `has_pointer`/`x`/`y` e `buttons`. Chamado em `Host_Frame` **depois** do
   `Mods_FramePre` e **antes** do input frame — é essa posição que faz o cursor e
   o clique valerem para o frame que está começando.
   O predicado **não** é `!= key_game` de propósito: isso incluiria o console
   (`key_console`) e o modo de mensagem, e abrir o console ligaria o painel do
   headset sem menu nenhum dentro dele (console e HUD ficam fora — decisão do dono
   em 2026-10-12: "só os menus").
2. **Canvas 2D** (`ref/gl/gl_fbo.c` + `engine/client/cl_view.c`): com
   `want_canvas` (e `menu_open` — trava dupla), o `V_PostRender` desvia os
   **menus** do frame (`VGui_Paint` + `UI_UpdateMenu`) para um alvo RGBA8 do
   tamanho do espaço 2D, criado sob demanda (`R_HudCanvasBegin`), e no fim devolve
   o resultado para a **janela** com blend (`R_HudCanvasEnd`) — sem isso o monitor
   ficaria sem menu. O Mod recebe o `texnum` e desenha o canvas nos olhos.
   **O canvas NÃO captura o HUD** (nem o console, nem os overlays de debug): eles
   são desenhados direto na janela, antes do `R_HudCanvasBegin`. Isso é requisito,
   não detalhe — o HUD é conteúdo de jogo e, capturado no canvas, aparecia **por
   cima do menu de compra** dentro do headset (o ícone verde de "zona de compra",
   o radar, a mira e vida/munição flutuando sobre os itens, atrapalhando a
   leitura). Foi assim que o dono descreveu o defeito em 2026-10-12: "um elemento
   azul/ciano que não é a tela, mas por cima dela". Medido: sem o HUD no canvas, o
   canvas do menu de compra perde ~3 mil pixels opacos (o ícone) e o menu fica
   idêntico.
3. **Cursor virtual** (`engine/client/input/input.c` + `cl_game.c`): a posição
   absoluta do cursor de menu tem **dois consumidores** e os dois passam a ler
   `IN_GetCursorPos` — `IN_MouseMove` (→ `UI_MouseMove`, a UI do engine) e o slot
   `GetMousePosition` do `cl_enginefunc_t` (→ `g_pMenu->MouseMove`, os menus do
   **client DLL**). Um só ponto de override: se o Mod escrevesse num só, o outro
   puxaria o cursor de volta e o hover piscaria. O clique é
   `IN_SetVirtualButton` → `IN_MouseEvent( 0, down )` → em `key_menu` cai em
   `Key_Event( K_MOUSE1 )` → `UI_KeyEvent` → o item do menu (`K_MOUSE1` = 241).

**`R_InvalidateTextures()`** (`ref/gl/gl_fbo.c`): o Mod desenha com texturas
**cruas** (`pglBindTexture`), que o cache do ref não vê — e o cache é consultado
por igualdade ("já está essa, não re-bindo"). Sem invalidar depois de cada ponto
em que o Mod desenha (`Mods_AfterEyes`, `Mods_Overlay2D`), o próximo `GL_Bind` do
engine pode **pular** o bind e a UI/3D sair com a textura do Mod.

**Orientação do painel (bug do espelho, 2026-10-12):** a âncora usa frente e
direita **horizontais** (`fwd = (cy, sy, 0)`, `right = fwd × up = (sy, −cy, 0)`) e
o Mod projeta o retângulo pelo frustum assimétrico de cada olho. Com a direita
escrita como `(−sy, −cy, 0)` (só o X invertido) o retângulo deixava de ser
perpendicular à frente e o painel saía **espelhado** em todo yaw com seno não nulo
e **cisalhado** nos demais — era o "menu espelhado" relatado no headset. A conta
fecha: `right` tem de ser `fwd × up`, como no `AngleVectors` do engine com
pitch/roll zero. Verificação sem headset: `/tmp/warp_panel.py` projeta o canvas
real com a mesma matemática do Mod e mostra o painel como ele sai no HMD —
comparando os dois vetores, o antigo sai espelhado e o novo correto; uma varredura
de 72 yaws mostra 70 falhas com o vetor antigo (só 0° e 180° passavam) e zero com
o corrigido.

**Debug sem headset:** o cvar privilegiado `hud_ptr_test` (default 0) força o
canvas e pilota o cursor; `hud_ptr_test 2` **abre e fecha o menu sozinho** (o
resto dos `+comandos` da linha de comando se perde no carregamento do mapa, então
não dá para encadear `+togglemenu`) e `hud_ptr_test 3` abre o **menu de compra
real** pelo comando de cliente `showvguimenu 28` (`MENU_BUY`, o mesmo caminho da
tecla `b`) e o deixa aberto — é o conteúdo que o dono vê no headset. Com `-console`
(modo janela só tem console com essa flag: `Con_Printf` sai cedo sem
`host.allow_console`) o rastro `hud_ptr_test:`/`UI_MouseMove`/`UI_KeyEvent` aparece
no stdout, e o `-dev 1` liga também o `Con_DPrintf` do `UI_MouseMove`/`UI_KeyEvent`.

O **conteúdo exato do painel** sai por `HUD_CANVAS_DUMP=/tmp/x.pam` (+
`HUD_CANVAS_DUMP_FRAME=<n>`, o n-ésimo frame de canvas — o 1º costuma ser cedo
demais), com um histograma de alfa no console: é o que separa "o jogo pintou" de
"o mundo vazou pelo alfa" e foi o que identificou o HUD sobre o menu. O dump do
**mod_vr** (`VR_PANEL_DUMP`) pega a **janela** (o canvas já blendado sobre o
espelho), então os dois são complementares: canvas = conteúdo do painel, janela =
caminho do monitor.

### Call sites no engine (implementados na Fase 1)

| Call site | Arquivo | Hook |
|---|---|---|
| `Host_Init` (após exec dos configs) | `engine/common/host.c` | `Mods_Init` — parse `-mod <nome>` + load |
| `Host_Frame` | `engine/common/host.c` | `Mods_FramePre` → **`CL_ModHud`/`Mods_Hud` (v8)** → `Host_InputFrame` → `Mods_Input` → … → `Mods_FramePost` |
| `CL_CreateCmd` (após o client DLL) | `engine/client/cl_main.c` | `IN_ModAppendMove` (v5) — aplica a amostra do Mod no `usercmd` |
| `Host_ShutdownWithReason` | `engine/common/host.c` | `Mods_Shutdown` (unload limpo) |
| `SCR_UpdateScreen` | `engine/client/cl_scrn.c` | `Mods_BeginFrame` (pré-3D), `Mods_EndFrame` (pós-render); o antigo `Mods_HudFrame` saiu na v8 (virou `CL_ModHud`, em `Host_Frame`) |
| `V_RenderView` (por view pass) | `engine/client/cl_view.c` | `Mods_Camera( &rvp, viewnum )` — pode sobrescrever a view |
| `V_RenderView` (por olho, **após** `Mods_Camera`) | `engine/client/cl_view.c` | `Mods_ViewModel` (v6) — arma na mão; aplica `clgame.viewent` + `RF_NO_VIEWMODEL`/`RF_VIEWMODEL_WORLD` no `rvp` |
| `V_PostRender` (HUD) | `engine/client/cl_view.c` | HUD/`SCR_TileClear` desenham **direto na janela** (não entram no canvas do headset) |
| `V_PostRender` (início dos menus) | `engine/client/cl_view.c` | `R_HudCanvasBegin` (v8) — desvia os MENUS (`VGui_Paint`/`UI_UpdateMenu`) para o canvas quando o Mod pede |
| `V_PostRender` (fim dos menus, pré-`SCR_MakeScreenShot`) | `engine/client/cl_view.c` | `R_HudCanvasEnd` (v8) — devolve o canvas para a janela (com blend) |
| `V_PostRender` (fim, pré-swap) | `engine/client/cl_view.c` | `Mods_Overlay2D` (v3) — overlay 2D por cima da UI; `R_InvalidateTextures` (v8) depois |
| `IN_MouseMove` / `GetMousePosition` | `engine/client/input/input.c`, `engine/client/dll_int/cl_game.c` | `IN_GetCursorPos` (v8) — cursor virtual do Mod, lido pelos dois consumidores |
| `Key_Event` (topo, após `Key_Rotate`) | `engine/client/input/in_keys.c` | `Mods_Key` (v7) — tecla antes de todo handler; pode consumir |
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
| `vr` (OpenXR, WiVRn/Quest 3) | display + hud | **em construção** — estéreo da Fase 2 feita e conferido (frustum assimétrico por olho + IPD real do runtime + fov/pose declarados iguais aos renderizados, 2026-10-12); painel de debug da tela 2D (`Mod_Overlay2D`); input nas Fase 3, HUD no headset na Fase 4 |
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
