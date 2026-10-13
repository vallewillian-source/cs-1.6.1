# PLAN.md — Mod VR para o CS 1.6 open-source (MVP)

> Status: planejamento · Criado em 2026-10-11
>
> Stack atual (rodando e validada): cliente **Xash3D FWGS (64-bit) + cs16-client** em `game/`,
> servidor dedicado **ReHLDS + ReGameDLL_CS** em `server/` (podman), fontes e builds em `src/`.
> Ambiente Fedora sem root (sudo indisponível): builds 32-bit do servidor via podman; cliente nativo.

---

## 1. Objetivo

Jogar o CS 1.6 atual (cliente em `game/`) em realidade virtual no Fedora, com **Meta Quest 3 via WiVRn** (PCVR: o jogo roda no PC e é transmitido ao óculos).

### 1.1 Definição de pronto do MVP

1. Jogo estereoscópico no Quest 3, com olhar 1:1 pela cabeça (pitch/yaw; câmera na altura dos olhos do player).
2. Locomoção e ações **inteiramente pelos controles do Quest** (thumbsticks + botões), com mira pela cabeça.
3. **HUD e menus legíveis e navegáveis dentro do óculos** (painel virtual + ponteiro laser nos controles), incluindo buy menu, scoreboard, console e menus do cs16-client.
4. Uma partida completa contra bots (YAPB) no servidor dedicado local, **sem nenhuma mudança no servidor**.
5. O modo flat atual permanece 100% intacto e é o fallback.
6. Tudo implementado como **Mod VR** — primeiro usuário de uma **arquitetura de Mods** reutilizável (seção 2).

### 1.2 Fora do escopo do MVP (futuro, contextualiza a arquitetura)

- Arma em 6DoF nas mãos / mira realista por controle — explicitamente cortado pelo usuário.
- Mãos, corpo, tracking facial, áudio espacial, passthrough.
- Multiplayer VR-vs-flat com balanceamento, conteúdo novo, performance extrema (foveação agressiva etc.).

---

## 2. Arquitetura proposta

### 2.1 Princípios

- **VR é 100% client-side.** Servidor (ReHLDS/ReGameDLL), protocolo de rede e conteúdo não mudam. Cliente VR e cliente flat jogam no mesmo servidor, indistinguíveis para ele.
- **"Mod" ≠ "game dir".** Um Mod é uma biblioteca (`mod_<nome>.so`) que o engine carrega por parâmetro (`-mod <nome>`), recebendo hooks em pontos de extensão definidos. Nada duplica o conteúdo de `cstrike/`; nada muda no jogo sem o Mod.
- **Contrato estável e versionado (C ABI).** O engine expõe uma interface única; Mods futuros (VR é o primeiro; depois: arma 6DoF, HD pack, gameplay, acessibilidade) usam a mesma interface. O engine **não** é re-patchado a cada Mod.
- **Fallback total.** Sem `-mod`, o comportamento é byte-a-byte o atual. Hooks não configurados = no-op.

### 2.2 Camadas

```
┌────────────────────────────────────────────────────┐
│ cs16-client (client_amd64.so, menu_amd64.so)       │ HUD/gameplay — inalterado no MVP*;
│                                                    │ desenha via API 2D do engine
├────────────────────────────────────────────────────┤
│ Xash3D FWGS — branch `vr-mod`                      │
│   + common/mod_api.h    contrato v1 dos Mods       │
│   + engine/mod_loader.c loader (dlopen) + hooks    │  ← ÚNICO patch estrutural
│   + ref_gl              renderer (patch mínimo p/  │
│                          projeção/câmera externa)  │
├────────────────────────────────────────────────────┤
│ mod_vr.so  ← PRIMEIRO MOD (OpenXR)                 │
│   sessão+estéreo · input dos controles ·           │
│   painel virtual de HUD/menus · pacing             │
├────────────────────────────────────────────────────┤
│ WiVRn — runtime OpenXR (Flatpak user, sem root)    │
├────────────────────────────────────────────────────┤
│ Meta Quest 3 — client WiVRn (Meta Store), Wi-Fi    │
└────────────────────────────────────────────────────┘
 * se hooks no engine não bastarem para algo do HUD,
   ajusta-se o cs16-client na branch `vr-mod` (espera-se mínimo).
```

### 2.3 Contrato de Mod (`mod_api.h`) — rascunho de design

Struct versionada, resolvida via um único símbolo exportado (`ModGetAPI`). Campos desconhecidos/`NULL` são ignorados pelo engine (compatibilidade para frente e para trás).

- **Identidade**: `api_version`, `mod_name`, `capabilities` (bitmask: `display`, `input`, `hud`).
- **Lifecycle**: `Mod_Init(engine_api)`, `Mod_Shutdown()`.
- **Frame**: `Mod_FramePre()` / `Mod_FramePost()` — pacing VR (frame sync OpenXR) e coleta de poses.
- **Display** (capability `display`): `Mod_Camera(eye, view_t*)` — sobrescreve origem/ângulos/projeção **por olho**; `Mod_BeginFrame(gl_ctx)`, `Mod_EndFrame()` — swapchains, submit OpenXR; engine reconhece o Mod como "display owner" (desliga mouse grab, vsync de janela, cursor de software).
- **Input** (capability `input`): `Mod_Input(input_sample_t*)` — injeta movimento/botões/ângulos pela mesma rota do teclado/mouse (client prediction intacta).
- **HUD** (capability `hud`): `Mod_HudRedirect(target)` — redireciona desenho 2D para o canvas do Mod; `Mod_HudPointer(x, y, click)` — ponteiro laser injeta mousemove/click no espaço 2D.

Regra de ouro: **o engine nunca conhece OpenXR**. Tudo que é VR vive no `mod_vr.so`.

### 2.4 Layout do código

```
cs-opensource/
├── src/xash3d-fwgs/        branch `vr-mod`: common/mod_api.h, engine/mod_loader.c,
│                           call sites de hooks, patch mínimo no ref_gl
├── src/cs16-client/        branch `vr-mod` (espera-se: vazia ou quase)
├── mods/
│   ├── vr/                 fontes do mod_vr (C++, OpenXR loader, GL)
│   └── null/               Mod-template log-only (exemplo de doc + 2º Mod de teste)
├── scripts/                build-openxr.sh · build-mods.sh · run-cs-vr.sh (novos)
├── docs/MODS.md            contrato da ABI + tutorial "como escrever um Mod"
├── PLAN.md                 este arquivo
└── README.md               ganha seção VR na Fase 5
```

---

## 3. Fases e tarefas

Convenções:
- Toda tarefa é **atômica**: uma responsabilidade, com critério de pronto verificável ("Pronto quando:").
- Sem estimativas de tempo (decisão do time). Complexidade relativa indicada: 🟢 baixa · 🟡 média · 🔴 alta.
- Cada fase termina com um **marcador** demonstrável antes de avançar.

### Fase 0 — Infraestrutura VR (queimar o risco do transporte antes de codar o jogo)

> ✅ **CONCLUÍDA em 2026-10-11.** 0.1/0.2 puladas por decisão do usuário (WiVRn já
> validado no ambiente). Detalhes: OpenXR-SDK **1.1.63** em `src/openxr-sdk/install/`
> (loader dinâmico + headers); branches `vr-mod` criadas nos dois repos (base:
> xash3d `590869d`, cs16-client `6a49bcc`); baseline flat em
> `backups/flat-baseline-20261011/` (13 arquivos + `MANIFEST.sha256`); rebuild das
> branches validado com boot + conexão ao dedicado; `docs/MODS.md` versionado.
> Pegadinha resolvida: a imagem `steamcmd:latest` mudou de base e passou a linkar
> `libbz2.so.1.0` (soname Ubuntu) — `build-xash.sh` normaliza para `libbz2.so.1`
> (Fedora) com `patchelf` no pós-build.

- [x] 🟢 **0.1** *(pulado — WiVRn já funciona no ambiente, pelo usuário)* Instalar WiVRn via Flatpak *user* (`io.github.wivrn.wivrn`) e instalar o client no Quest 3 pelo wizard do dashboard. *Pronto quando:* headset conecta, mostra a tela virtual e **reproduz o áudio do PC no óculos**.
- [x] 🟢 **0.2** *(pulado — WiVRn já funciona no ambiente, pelo usuário)* Rodar um app OpenXR de teste independente (hello_xr ou equivalente, backend GL) contra o runtime do WiVRn. *Pronto quando:* cena estéreo visível no headset com head tracking — prova o combo Fedora + RTX 4060 + WiVRn + GL.
- [x] 🟡 **0.3** Criar `scripts/build-openxr.sh` (rootless): baixar e buildar OpenXR-SDK (loader + headers), no container podman ou vendored. *Pronto quando:* `libopenxr_loader.so` + headers disponíveis para link.
- [x] 🟢 **0.4** Criar branch `vr-mod` em `src/xash3d-fwgs` e `src/cs16-client`; salvar baseline dos binários flat atuais (`.so` + `xash3d`). *Pronto quando:* build da branch `vr-mod` sem alterações reproduz o jogo flat funcional (smoke test).
- [x] 🟢 **0.5** Criar esqueleto de `docs/MODS.md` e registrar a decisão de ABI (struct versionada + dlopen + capabilities). *Pronto quando:* documento commitado na branch.

**Marcador da fase:** transporte Quest 3 ↔ Fedora provado ponta a ponta, sem root, com um app que não é o nosso.

### Fase 1 — Loader de Mods no engine (fundação da arquitetura)

> ✅ **CONCLUÍDA em 2026-10-11.** Commit `8d12c80` na branch `vr-mod` do xash3d-fwgs;
> template em `mods/null/`; contrato v1 congelado em `docs/MODS.md`.
> Descoberta de arquitetura: `COM_LoadLibrary`/`FS_FindLibrary` força **lowercase** no
> path da biblioteca (`// always in lower case (why?)` em `filesystem/searchpath.c`) —
> o loader de Mods usa `dlopen` direto (POSIX) e valida o path com `stat`, resolvendo o
> diretório do binário via `/proc/self/exe` (case garantido). Mod loga em arquivo próprio
> (`game/mods/mod_null.log`) porque o `Con_Printf` do engine não vai para stdout em modo
> gráfico (o canal confiável de log é `-log <arquivo>`, que exige path iniciando com alfanumérico).
> Descoberta de conteúdo: os `+comandos` da linha de comando (`+connect` etc.) só rodam se um
> `.rc` invocar `stuffcmds` — `valve/valve.rc` cobre o HL, mas o CS do HLDS não traz `cstrike.rc`;
> criamos `game/cstrike/cstrike.rc`. Logging do dedicado ligado (`log on` no `server.cfg`) —
> sem isso nenhum join/drop aparece em lugar nenhum (stdout do HLDS é bufferizado).

- [x] 🟡 **1.1** Escrever `common/mod_api.h` v1 (contrato da seção 2.3: structs, hooks, capabilities). *Pronto quando:* header compila standalone e é consumido por um stub. — compilou com `-Wall -Wextra` no `mods/null`.
- [x] 🟡 **1.2** Implementar o loader no engine (`engine/common/modloader.c`): parse de `-mod <nome>`, `dlopen("mod_<nome>.so")`, resolve `ModGetAPI`, valida `api_version`, guarda globals; unload limpo no shutdown. *Pronto quando:* `-mod null` carrega e descarrega sem crash, com log claro. — validado em modo dedicado (stdout) com ciclo completo load→init→frames→unload.
- [x] 🟡 **1.3** Inserir call sites de hooks nos pontos neutros do host: início/fim de frame, pré-setup de câmera (por olho), pós-desenho de HUD, fila de input, criação do contexto GL. *Pronto quando:* cada call site é no-op sem Mod carregado. — `host.c` (frame/input/shutdown/init), `cl_scrn.c` (begin/hud/end), `cl_view.c` (câmera por view pass), `vid_sdl2.c` (video init).
- [x] 🟢 **1.4** Implementar o Mod `null` (template): loga cada hook com contadores e timestamp. *Pronto quando:* 10+ min de jogo flat com `-mod null`, sem crash e sem alteração de gameplay; vira exemplo do `MODS.md`. — teste longo de 10,5 min conectado ao dedicado.
- [x] 🟢 **1.5** Regressão flat: comparar comportamento sem `-mod` vs baseline (Fase 0.4) e com `-mod null`. *Pronto quando:* zero diferença funcional. — boot + connect sem `-mod` após as mudanças, idêntico ao baseline funcional.

**Marcador da fase:** o Mod `null` prova que a arquitetura é genérica — nada nela é VR-específico.

### Fase 2 — VR core no mod_vr (sessão OpenXR + render estéreo)

> ✅ **IMPLEMENTADA em 2026-10-11 — código completo e testado sem headset; a validação
> final em estéreo exige o Quest 3 conectado (WiVRn).** Commit do engine na branch
> `vr-mod` (ABI v2 + loop de olhos + flag de viewport); mod em `mods/vr/` +
> `scripts/run-cs-vr.sh`.
>
> Descobertas desta fase (todas documentadas no código):
> - O WiVRn 26.9 (RPM) só aceita `xrCreateInstance` de API **1.1** (1.0 → `-51`) e **bloqueia
>   para sempre** sem headset conectado (o `hello_xr` do sistema trava do mesmo jeito) → o
>   init do OpenXR roda em **thread separada com timeout de 6 s**; o jogo fica flat e a VR
>   "liga sozinha" quando o headset conectar. Runtime manifest: `/usr/share/openxr/1/openxr_wivrn.json`.
> - O `ref_gl` tem um *scene target* interno (`r_scene_scale`/MSAA) que rebinda FBO por cima
>   do nosso — desligado por cvars no launcher; e o math do viewport assume viewport ⊆ janela
>   → novo flag `RF_VIEWPORT_ABSOLUTE` (`ref_params.h`) com branch no `R_SetupGL` (patch mínimo).
> - Ubuntu linka com `--as-needed`: na linkagem do mod, o fonte vem ANTES das `-l`; e
>   `ModGetAPI` em C++ precisa de `extern "C"`.
> - Fallback do 2.5 em vigor: FOV simétrico = máx por eixo no render; o **submit** usa o
>   FOV assimétrico real por olho (compositor não distorce).
> - O **swap acontece dentro do `V_PostRender`** (`R_EndFrame` → `GL_SwapBuffers`), ou
>   seja, muito antes do `Mod_EndFrame` (fim do `SCR_UpdateScreen`): desenhar no
>   `Mod_EndFrame` cai no back buffer pós-swap e nunca aparece. Overlay 2D de verdade
>   precisa de um ponto próprio logo antes do `R_EndFrame` → **`Mod_Overlay2D` (ABI v3,
>   `MOD_CAP_HUD`, tarefa 2.9)**. Também por isso o `Mod_AfterEyes` (pré-2D) serve só
>   para o espelho: o HUD do jogo desenha por cima dele.

- [x] 🟡 **2.1** Esqueleto do mod_vr: instance/session OpenXR contra o WiVRn (via `XR_RUNTIME_JSON`), reference space `LOCAL`, spaces de head e hands. *Pronto quando:* log confirma sessão criada e poses chegando (sem render). — log em `game/mods/mod_vr.log`; poses exigem headset.
- [x] 🟡 **2.2** Frame loop OpenXR (`xrWaitFrame` → `xrBeginFrame` → `xrLocateViews` → `xrEndFrame`) acoplado a `Mod_FramePre/Post`, com pacing pelo `predictedDisplayTime`. *Pronto quando:* loop estável no refresh do HMD (72/90 Hz). — código pronto; estabilidade no HMD pendente de headset.
- [x] 🔴 **2.3** Swapchains GL por olho (`XR_KHR_opengl_enable`): criar swapchains, FBO por image, bind no hook de câmera. *Pronto quando:* cor de clear distinta por olho visível no headset. — código pronto (`VR_DEBUG_CLEAR=1` pinta olhos de azul/vermelho p/ teste).
- [x] 🔴 **2.4** Camera override + render duplo: origem/ângulos do HMD → view do engine, cena desenhada **2× por frame** (um por olho), offsets apenas de render (física/prediction intocadas). Desligar paths desktop: vsync de janela. *Pronto quando:* em pé no mapa, olhar 1:1. — eye loop no `V_RenderView` (`Mods_EyeCount`), áudio só no olho 0; pendente de headset.
- [x] 🔴 **2.5** Projeção por olho: fallback do MVP em vigor (FOV simétrico = máx por eixo; submit assimétrico real). *Pronto quando:* estéreo confortável, sem clipping grave. — pendente de headset; injeção de matriz assimétrica no ref_gl fica pro pós-MVP.
- [x] 🟡 **2.6** Submit + mirror: `xrEndFrame` com projection layers; espelho do olho esquerdo na janela via `Mod_AfterEyes` (blit antes do 2D — monitor mostra vista + HUD flat). — pendente de headset p/ submit real.
- [x] 🟢 **2.7** `VR_RESOLUTION_SCALE` (0.3–3.0, default 1.0) via env. *Pronto quando:* escala default sustenta refresh estável na RTX 4060. — medição de fps no HMD pendente de headset.
- [ ] 🟡 **2.8** (Condicional) Foveação fixa via extensão FB, se o WiVRn expuser. *Pronto quando:* ganho mensurável de margem de performance. — não tentada (condicional; sem headset não há como medir).
- [x] 🟢 **2.9** **Painel de debug na tela 2D** (2026-10-12): FPS + todo erro/aviso do carregamento do VR, visível **só no monitor** e só quando o jogo roda com `-mod vr`. Exigiu a ABI **v3** (`Mod_Overlay2D`, capability `MOD_CAP_HUD`), chamado no fim do `V_PostRender` — depois do HUD/menus e antes do swap, o único ponto em que um Mod pinta por cima da UI (e, no VR, fora das swapchains dos olhos). Fonte bitmap 8x16 embutida (`mods/vr/gen_font.py` → `vr_font.h`); `VR_DEBUG_PANEL=0` esconde, `VR_PANEL_DUMP=saida.ppm` salva um quadro para diagnóstico. *Pronto quando:* com o VR falhando (sem runtime/headset) o painel mostra o `XrResult` traduzido, o runtime e o motivo do fallback flat. — validado sem headset (dump conferido: painel por cima do menu, 1 erro + 1 aviso detalhados).
- [x] 🔴 **2.10** **Correções para o VR subir de verdade com o WiVRn** (2026-10-12): o painel de 2.9 mostrou que o caminho VR nunca havia rodado de ponta a ponta. Três causas raiz, todas corrigidas no `mod_vr.cpp` + launcher:
  (a) **driver de vídeo**: numa sessão Wayland o SDL abria no driver `wayland` (EGL) e não existe contexto GLX — o mod é Xlib/GLX, então `run-cs-vr.sh` agora exporta `SDL_VIDEODRIVER=x11` (XWayland serve);
  (b) **requisitos gráficos**: faltava `xrGetOpenGLGraphicsRequirementsKHR`, obrigatória antes de `xrCreateSession` com `XR_KHR_opengl_enable` — sem ela o WiVRn devolve `-50 XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING`;
  (c) **thread do contexto**: o client compositor do runtime faz `glXMakeCurrent` na thread que cria a sessão, mas a sessão era criada na thread de init (o contexto GL pertence à main thread) → `XR_ERROR_INITIALIZATION_FAILED`. Agora só o `xrCreateInstance` (que bloqueia sem headset) roda fora; `xrGetSystem`/requisitos/sessão/reference space rodam na main thread, com retry de 1 em 1 s (headset conectado depois = VR liga sozinha).
  Também corrigidos de passagem: `glXQueryContext` devolve **0 em sucesso** (a checagem estava invertida e o binding saía com `visual 0`/`fbconfig nulo`), formato de swapchain passou a ser escolhido da lista do runtime (era `0`, inválido), e o aviso de "2 wivrn-server" do launcher não confunde mais o pai com o worker.
  *Pronto quando:* a inicialização chega a `xrBeginSession` + swapchains por olho com o runtime WiVRn. — **validado até swapchains** sem headset (`sessão READY`, `xrBeginSession`, 2476x2594 por olho, RGBA8, 0 erros no painel); falta confirmar o estéreo com o Quest 3 conectado.
- [x] 🔴 **2.11** **HMD preto mesmo com tudo inicializado** (2026-10-12): com o Quest 3 ligado a sessão ficava presa em `READY` e o headset mostrava preto, sem erro em lugar nenhum. Causa: **faltava o bombeamento de frames**. O app só chamava `xrWaitFrame`/`xrBeginFrame` em `VISIBLE/FOCUSED`, mas o OpenXR exige que ele continue rodando o frame loop já em `SYNCHRONIZED` (submetendo quadro vazio, `layerCount 0`) — sem isso o runtime não avança e nunca entrega VISIBLE. Corrigido com `xr_session_begun` (ligado no `xrBeginSession`, desligado em STOPPING/LOSS): o loop bombeia a partir do begin e só renderiza nos olhos quando VISIBLE/FOCUSED. Verificado com o headset conectado: `READY → SYNCHRONIZED → VISIBLE → FOCUSED`, dois olhos renderizados, `xrEndFrame` com `camadas=1` e conteúdo real nos olhos.
- [x] 🟢 **2.12** **Diagnóstico do pipeline estéreo** (2026-10-12): como o HMD preto não deixa rastro, o Mod agora mede e registra o que importa: completude do FBO de cada olho (`glCheckFramebufferStatus`), binding do FBO depois do render dos olhos, **leitura de pixels da imagem da swapchain** (com conteúdo = o render aconteceu; preto = o problema é o render), `xrEndFrame` com o número de camadas e o resultado, `xrLocateViews` (flags das poses) e erros de GL agregados. Traços dos primeiros 12 frames do estéreo (`VR_TRACE=1` mantém ligado); avisos a cada ~5 s dizendo *onde* parou ("ainda nao VISIBLE" vs "sem render 3D nos olhos"). `VR_PANEL_DUMP_AT=<s>` escolhe o atraso do dump. *Lição registrada:* o engine **não tem rampa de gama de hardware** (não existe `SetWindowGammaRamp` no código), então o que está no framebuffer é o que o monitor mostra — não há diferença de gama entre espelho e HMD.
- [x] 🟢 **2.13** **Desempenho: o loop estava preso no vsync do monitor** (2026-10-12): com o VR já funcionando, o jogo travava e apareciam **áreas pretas nas partes novas do mapa ao virar a cabeça**. A medição (`perf`, uma linha por segundo com trabalho por frame vs período do HMD) mostrou o quadro exato do problema: **60 fps (16,7 ms) contra um HMD a 90 Hz (11,1 ms)** — 1 de cada 3 quadros do headset chegava sem conteúdo novo, e é justamente isso que deixa preto o que acaba de entrar no campo de visão. Causa: o `-gl_vsync 0` do launcher **não segurava** (a forma `-cvar` é aplicada antes dos configs e o `config.cfg` do jogador religa o vsync), então a troca de buffers da JANELA travava o loop inteiro — engine + VR — no refresh do monitor. Correção de uma linha no `run-cs-vr.sh`: passar por **comando de console** (`+gl_vsync 0 +fps_max 200 +r_scene_scale 1 +gl_msaa 0 +r_speeds 0`), que roda depois dos configs. Medido: **60 → 90 fps**, casando com o período do HMD. Folga verificada com `VR_RESOLUTION_SCALE`: 1.0 (2476x2594/olho) = 1,5 ms de trabalho; 1.3 = 1,9 ms; 1.6 (3961x4150/olho) = 2,4 ms — ou seja, sobra orçamento para subir a resolução se quiser nitidez, e há margem para descer se a GPU/encoder não acompanhar.

- [x] 🔴 **2.14** **Estéreo errado: o par não fundia ("cada olho sozinho parece ok, os dois juntos não")** (2026-10-12). Três causas somadas, todas medidas com o Quest 3 conectado:

  1. **Frustum assimétrico por olho** (a maior). O runtime declara, por olho, quatro ângulos independentes e espelhados — no Quest 3 via WiVRn: esquerdo `L54 R40 U44 D55`, direito `L40 R54 U44 D55`. São 94°×99° **fora de centro** (assimetria ±14° na horizontal, 11° na vertical, com sinal trocado entre os olhos). O engine só sabia montar frustum **centralizado** (`fov_x`/`fov_y`) e o mod só tinha isso: renderizava 54°×55° centrados e submetia 94°×99° assimétricos — o compositor esticava a imagem de cada olho por um fator **diferente**, e é exatamente isso que impede a fusão. Exigiu a **ABI v4**: `mod_view_t.fov_asym[4]`/`has_fov_asym` → `RF_FOV_ASYM` no `ref_viewpass_t` → projeção off-center em `R_SetupProjectionMatrix` (`Matrix4x4_CreateProjection` já aceitava limites assimétricos, o `ref` é que sempre passava simétricos).
  2. **Rotação XR→GoldSrc espelhada.** O mapa `(x,y,z) → (-z, x, y)` tem determinante **−1**, e os dois espaços são **destros** (OpenXR: X direita/Y cima/Z trás; GoldSrc: X frente/Y esquerda/Z cima — conferido pelo `right × up = -forward` do `AngleVectors`). Logo o mapeamento era um **espelho**, não uma rotação: a cabeça girava para o lado errado (yaw com sinal trocado) e — pior para o estéreo — o eixo "direita" da cabeça saía invertido, jogando o offset dos olhos para o lado errado (par pseudo-escópico). O correto é `(-z, -x, y)`. Conferido numericamente contra o `AngleVectors`/modelview do engine (erro ~1e-4 em poses reais do WiVRn).
  3. **IPD chutado.** O mod afastava as câmeras em 63 mm fixos; o headset reporta **68,3 mm** (com o IPD calibrado do usuário). Agora o offset sai da **posição real de cada olho** informada pelo runtime (delta entre os olhos, trazido para o referencial da cabeça): medido `(68,3 0,0 −0,0) mm` — separação pura ao longo da direita da cabeça, câmeras paralelas. `VR_IPD=<m>` força um valor manual.

  O **roll** da cabeça também passou a entrar (`atan2(-right.z, up.z)`), porque com frustum assimétrico o centro ótico sai do eixo da view e inclinar a cabeça deslocaria a imagem.

  *Verificação objetiva (`VR_STEREO_DUMP=pref` grava as duas imagens do MESMO quadro):* a paralaxe medida entre os olhos é **−632 px** contra **−600 px** previstos pela geometria declarada (a diferença de ~32 px é a paralaxe da cena, ~2,2 m de profundidade média); **disparidade vertical de 1 px**; correlação por região **0,93–0,97** (antes, com o render simétrico contra o submit assimétrico, não havia deslocamento único que alinhasse as duas imagens). O teste de espelho dá pico pior (0,32 em −1112 px), confirmando que o par não está espelhado. Ferramenta de medição descartável em `/tmp/pair_final.c`.

- [x] 🔴 **2.15** **O painel de debug (e o HUD do jogo) apareciam no headset, não no monitor** (2026-10-12). Causa: **o alvo do 2D no fim do frame não era a janela**. Com o VR ativo o frame termina com o FBO da imagem do último olho ligado — o `blit_mirror` do espelho salva e *devolve* o binding que encontrou — e o `R_Set2DMode(true)` chama `GL_BindScreenTarget()`, que **retornava sem bindar** quando não há render target interno (o caso do VR: `+r_scene_scale 1 +gl_msaa 0`). Resultado: painel de debug, HUD, menus e console eram desenhados DENTRO da imagem enviada ao compositor — 2D na tela errada, em um olho só e ainda deformado pelo warp do runtime. Duas correções: no engine, `GL_BindScreenTarget` passa a ligar a janela explicitamente quando não há target interno (`ref/gl/gl_fbo.c`); no mod, o painel liga a janela (FBO 0) por conta própria antes de pintar e devolve o binding depois. Diagnóstico permanente: se o 2D voltar a chegar com FBO ≠ 0 aparece `painel: o 2D chegou com FBO N ligado` no `mods/mod_vr.log`. *Efeito colateral desejado:* o HUD/menu do jogo também **deixam de vazar** para o HMD — o headset volta a mostrar só o mundo até a Fase 4.

**Marcador da fase (maior risco técnico queimado):** *dust2 em estéreo, olhando ao redor, jogo 100% funcional por teclado/mouse.* → código completo e **estéreo geometricamente conferido**; falta a confirmação de conforto visual com o headset na cara (`scripts/run-cs-vr.sh` com o WiVRn conectado).

### Fase 3 — Input VR (controles Quest)

> Decisões tomadas com o dono em 2026-10-12 antes de implementar: **mira pela
> cabeça nesta fase** (arma na mão fica para depois), **só física do CS** (sem
> modo voo/noclip; o eixo vertical usa o `upmove` do jogo e portanto só age onde
> ele já age — água, espectador), **só snap turn** (sem giro suave) e
> **roomscale ligado** (a posição real do HMD move a câmera; agachar de verdade
> vira `IN_DUCK`).

- [x] 🟢 **3.1** Action set OpenXR: thumbsticks, triggers, squeeze, A/B/X/Y, menu, haptic em `/user/hand/*`; poses `aim` por mão. Implementado em `mods/vr/mod_vr.cpp` (seção "input VR"): action set `gameplay`, 14 ações, espaços de pose por mão e bindings sugeridos para `oculus/touch_controller`, `meta/touch_controller_plus` e o fallback `khr/simple_controller` (erro por perfil é tolerado e logado). `xrAttachSessionActionSets` roda no fim do `xr_setup_session`, ou seja **antes** do `xrBeginSession`. *Pronto quando:* o bloco `INPUT` do painel mostrar o perfil de interação por mão — é ele que responde "o controle casou?" (se vier `(nenhum)`, nenhum binding pegou).
- [x] 🟡 **3.2** Locomoção: stick esquerdo → forward/side **relativo ao yaw da cabeça**, com deadzone radial (que é *piso*, não degrau: o resto da faixa é reescalado) e `walk` segurando grip (`cl_movespeedkey`, o mesmo fator do `+speed`). A orientação pela cabeça não custa conta no Mod: ele injeta o **yaw do HMD** como `viewangles_abs` e manda o stick cru — o `pm_shared` monta o `wishdir` com `AngleVectors(cmd.viewangles)` e ignora o pitch no chão. Com o stick centrado o teclado continua valendo (ver 3.7). *Pronto quando:* andar/strafe no jogo só com o controle.
- [x] 🟡 **3.3** Snap turn: stick direito horizontal → passo de `VR_TURN_SNAP_ANGLE` (padrão 30°), com **latch** por lado (dispara uma vez e só re-arma quando o stick volta a |x| < 0,3, senão giraria em rajada com o stick segurado). **Desvio do plano original:** não é via rotação do reference space — é um **offset de yaw** somado à pose do HMD, aplicado **igualmente no cmd e no render** (`IN_ModAppendMove` e `VR_Camera`). Sem essa simetria o corpo atiraria para um lado e a câmera olharia para o outro depois do primeiro passo; e o offset é o que também dá sentido ao offset de roomscale (o mundo é o playspace rodado do offset). O botão menu **recentra** (zera o offset, recalibra a altura) e o evento `REFERENCE_SPACE_CHANGE_PENDING` do runtime reancora sozinho — sem ele um recenter do headset teleportaria o jogador. *Pronto quando:* virar em passos sem drift e sem precisar de recenter constante.
- [x] 🟡 **3.4** Botões → ações do jogo, injetados pela rota de input do engine: trigger direito/esquerdo = fogo/ataque secundário, A = pulo, B = reload, X = use, Y = **placar** (segurar) e click do stick esquerdo = agachar (além do agachar físico). **Dois desvios do plano, deliberados:** `Y = lanterna` virou placar e `menu = ESC virtual` virou **recentrar** — abrir o menu do jogo seria inútil agora, porque **o 2D ainda sai no monitor, não no headset** (isso é a Fase 4); recentrar, sim, é necessário justamente por causa do snap turn. Mapear isso é uma linha quando a Fase 4 chegar. *Pronto quando:* todas as ações executam na partida.
- [x] 🟢 **3.5** Mira pela cabeça: `viewangles_abs` = pose do HMD, com pitch travado em ±89 (mesma trava do `CL_CreateMove` do cliente — o render pode olhar mais longe, o que vai ao servidor não). O `roll` do cmd fica 0 de propósito: o jogo ignora roll e o roll do render vem do `Mod_Camera`, que continua sendo a única fonte visual. *Pronto quando:* acertar alvos fixos e móveis de forma consistente — **e o teste decisivo, que também prova a rota de rede: atirar numa parede e o furo aparecer onde se olha.**
- [ ] 🟢 **3.6** (Opcional, cortável) Haptic curto ao atirar. A ação de vibração **já existe e está bindada** (`/user/hand/*/output/haptic`), mas `xrApplyHapticFeedback` ainda não é chamado — falta só ligar.
- [x] 🟢 **3.7** (Novo, saiu do desenho) **ABI v5 e aplicação no engine.** O `MOD_CAP_INPUT` existia desde a v1 mas era inerte (`Mods_Input` descartava a resposta). A v5 acrescentou `move_up`/`viewangles_abs`/`has_viewangles_abs` e ligou a aplicação: `IN_ModAppendMove` roda no `CL_CreateCmd` **depois** do client DLL (por isso o Mod vence o mouse sem desligá-lo, e o mouse segue navegando menus) e **antes** do `CL_PredictMovement`. Detalhes em `docs/MODS.md` §"Input". Regressão flat garantida por construção: fora de sessão ativa o Mod devolve a amostra zerada e o jogo fica idêntico ao de antes.
- [x] 🟢 **3.8** (Novo) **Roomscale e agachar físico.** A posição do HMD entra na câmera como offset **horizontal** (rotacionado pelo offset de snap, limitado por `VR_ROOMSCALE_MAX`) — inclinar-se funciona. O eixo **vertical não** entra na câmera: a queda da cabeça além de `VR_CROUCH_DROP` (com histerese) vira `MOD_BTN_CROUCH`, e quem baixa a vista é o duck do jogo. É de propósito — somar os dois rebaixaria a vista duas vezes. `VR_CROUCH_MODE` cobre quem joga sentado. *Custo assumido:* inclinar-se move a câmera sem mover a caixa de colisão, então dá para "espiar através" da geometria — comportamento padrão em shooters VR.
- [x] 🟢 **3.9** (Novo) **Vignette de conforto** (`VR_COMFORT_VIGNETTE`), desenhada na própria imagem do olho antes do `xrReleaseSwapchainImage`. **Padrão desligada** (0) — o plano previa intensidade leve por padrão, mas com snap turn (a opção mais confortável) ela é menos necessária e mexer na imagem do olho é exatamente onde já erramos duas vezes; fica a uma variável de distância depois da validação. Estado de GL salvo e restaurado, então o cache do ref_gl continua válido.

**Estado:** código completo, engine e mods compilados e instalados, **ABI v5 validada ponta a ponta sem headset** (`mods/null`: `loaded "null" (api 5)` com 237 frames e hooks pareados; `mods/vr`: `loaded "vr" (api 5, capabilities: display input hud)`). **Falta a validação com o headset**, que é a única que prova a locomoção: andar girando a cabeça, snap sem repetição, cada botão, agachar físico rebaixando uma vez só, o furo de bala onde se olha, e o menu do jogo (aberto pelo teclado) continuando a responder ao mouse com o VR sem mover o jogador.

**Marcador da fase:** partida jogável de ponta a ponta **sem teclado nem mouse** (spawn → andar → atirar → recarregar).

### Fase 4 — HUD e menus em VR

- [x] 🔴 **4.1** Camada de captura 2D: hook pós-HUD redireciona todo desenho 2D (HUD do cs16-client, menus do menu_amd64, console, texto do engine) para um FBO offscreen (canvas virtual). *Pronto quando:* canvas contém HUD e menus renderizados (verificável via dump de textura). — implementado como **canvas do ref** sob demanda (`R_HudCanvasBegin/End/Info`, ABI **v8**): o `V_PostRender` desvia o 2D para um alvo RGBA8 do tamanho do espaço 2D e, no fim, devolve o resultado para a janela com blend (o monitor continua com HUD/menu). O Mod recebe o texnum em `Mod_Hud`. Canvas = tamanho da janela (o mesmo espaço em que o mouse do engine é medido). Validado em run flat: o menu aparece no dump vindo do canvas, e o canvas **só existe quando alguém pede** (nenhuma criação no jogo normal). **Recorte corrigido em 12/10 (defeito relatado pelo dono):** o canvas passou a capturar **só os menus** — HUD, console e overlays de debug desenham direto na janela — e o predicado virou `cls.key_dest == key_menu` (não `!= key_game`, que ligava o painel no console). Motivo: com o HUD dentro do canvas, o ícone verde de "zona de compra" (e, em partida, radar/mira/vida/munição) aparecia **por cima do menu de compra** no headset — "um elemento azul/ciano que não é a tela, mas por cima dela". Provado com `HUD_CANVAS_DUMP` (PAM RGBA do conteúdo do painel + histograma de alfa): antes o canvas tinha o ícone; depois, só o menu, com o menu idêntico.
- [x] 🟡 **4.2** Painel virtual: canvas → textura de um quad no mundo; distância/escala calibráveis. *Pronto quando:* HUD e menus nítidos e legíveis dentro do óculos. — implementado com **âncora congelada no mundo** (na frente e na altura dos olhos **no instante em que o menu abre**, sem seguir a cabeça — decisão do dono em 12/10), quad **subdividido** (grade 6×6: a projeção de um retângulo é uma homografia e a grade mata o erro da interpolação afim), moldura escura, desenhado por olho em `Mod_AfterEyes` (antes do espelho, então o monitor também mostra o painel). Calibração ao vivo em `game/mods/vr_menu.cfg` (`dist_m`, `height_m`, `yaw_off_deg`, `y_off_m`, relido por mtime; salvar já aplica, mesmo com o menu aberto) + `VR_MENU_DIST`/`VR_MENU_HEIGHT`. A projeção do painel usa a **mesma fórmula do engine** (validado numericamente: 0,000000 px de diferença em 32 casos com o fov assimétrico real do Quest 3). **Bug do espelho (12/10, relatado pelo dono: "aparece espelhado horizontalmente"):** a "direita" do painel estava escrita como `(−sy, −cy, 0)` — só o X invertido — e a correta é `fwd × up = (sy, −cy, 0)`, a mesma do `AngleVectors` com pitch/roll zero. Com o sinal errado o retângulo deixava de ser perpendicular à frente: **espelhado** em todo yaw com seno não nulo e **cisalhado** nos demais (varredura de 72 yaws: 70 falhas com o vetor antigo — só 0° e 180° passavam — e zero com o corrigido). Verificado **sem headset** projetando o canvas real com a matemática do Mod (`/tmp/warp_panel.py`): o vetor antigo reproduz o menu espelhado, o novo sai correto, na mesma escala/posição. **Falta o olho do dono** para a legibilidade/posição.
- [x] 🟡 **4.3** Ponteiro laser: raio da pose `aim` do controle intersecta o quad → mapeia para coordenadas 2D do canvas → injeta mousemove/click. *Pronto quando:* navegar menu principal, **buy menu** e console usando só o controle. — implementado: laser da **mão direita** (`xr_act_aim[1]`), interseção com o plano do painel → pixel do canvas; o engine ganhou o **cursor virtual** (`IN_GetCursorPos`) lido pelos **dois** consumidores de cursor (o `UI_MouseMove` do engine e o `GetMousePosition` que o client DLL usa nos menus dele — sem isso um puxaria o cursor de volta e o hover piscaria) e o clique é `IN_MouseEvent` → `Key_Event(K_MOUSE1)` → `UI_KeyEvent`. O gatilho direito vira clique (e não `ATTACK`) com menu aberto; o snap turn fica bloqueado (decisão do dono). Validado **sem headset** com o cvar de debug `hud_ptr_test` (dirige o cursor e o clique; `2` abre/fecha o menu sozinho): o clique injetado **ativou um item do menu** (abriu o Console: `key_dest: menu → console`), e a UI recebeu a posição injetada (1336 chamadas de `UI_MouseMove`). O teste numérico pegou um **sinal trocado** no `t` do raio antes de ir para o headset. **Falta o olho do dono** para o conforto de apontar.
- [ ] 🟢 **4.4** Scoreboard e chat: confirmar que fluem pelo mesmo canvas (client HUD + texto do engine). *Pronto quando:* legíveis em partida. — *desenho do recorte de 12/10:* hoje o canvas **só liga com menu aberto**; ligá-lo sempre (HUD/placar no headset) é só trocar a política no Mod e testar o custo.
- [ ] 🟢 **4.5** Overlay de debug do Mod (fps do HMD, poses, tempo de submit) atrás de `vr_debug 1`. *Pronto quando:* diagnóstico possível sem sair do óculos. — o painel de debug 2D (na janela) já cobre isso parcialmente e ganhou as duas linhas dos menus; no headset ainda não.

**Marcador da fase:** fluxo completo no óculos — *menu → entrar na partida → comprar equipamento → jogar um round → scoreboard.*

### Fase 5 — Integração, experiência e fechamento do MVP

- [ ] 🟢 **5.1** `scripts/run-cs-vr.sh`: configura `XR_RUNTIME_JSON` do WiVRn, lança o engine com `-mod vr`; sem headset detectado → fallback flat com aviso. *Pronto quando:* um comando sobe o jogo em VR.
- [ ] 🟢 **5.2** Desktop entry "CS 1.6 VR" no GNOME (ícone dedicado, `StartupWMClass` próprio). *Pronto quando:* lançamento pelo menu do GNOME validado com `gtk-launch`.
- [ ] 🟢 **5.3** Cvars de usuário finais (`vr_*`): snap turn, distância/escala do HUD, resolution scale, recenter (combo de botões), modo do painel — persistidos no config e documentados. *Pronto quando:* ajustáveis sem rebuild.
- [ ] 🟡 **5.4** Teste end-to-end MP: cliente VR conecta no dedicado ReHLDS local e completa partida vs YAPB; logs do servidor comprovam cliente VR indistinguível de um cliente flat. *Pronto quando:* partida completa limpa, sem erros no log do servidor.
- [ ] 🟢 **5.5** Checklist de conforto: sessão de 20+ min, recenter acessível, documentação de enjoo (snap turn default, walk com grip, pausas). *Pronto quando:* checklist documentado e cumprido pelos testadores.
- [ ] 🟢 **5.6** README: seção VR (pré-requisitos, rede 5/6 GHz, solução de problemas) + `docs/MODS.md` finalizado. *Pronto quando:* pessoa nova instala e roda seguindo apenas a documentação.
- [ ] 🟢 **5.7** Regressão final do modo flat contra o baseline da Fase 0.4. *Pronto quando:* checklist flat passa integralmente.

**Marcador da fase:** critérios de aceite da seção 4 todos verdes.

### Fase 6 — Jogabilidade de armas VR, etapa 1: arma na mão e mira pela mão

> **Nota de ordem:** esta fase foi implementada ANTES das Fases 4 e 5 (12/10) porque
> não depende delas — é toda no cliente, sem servidor e sem HUD. A numeração dela fica
> no fim só para não renumerar as fases existentes. O MVP da seção 4 é definido com
> "mira pela cabeça", então armas VR são explicitamente **pós-MVP**.
>
> **O que a viabiliza (medido no código, não suposto):** o servidor calcula a direção do
> tiro em `v_angle + punchangle` (`ReGameDLL_CS/…/weapons.cpp`) e o `v_angle` vem do
> `usercmd` — o **mesmo** canal que o `pmove` usa para montar o `wishdir`
> (`ReHLDS/…/sv_user.cpp`). Como o Mod já escreve esse canal desde a Fase 3, "atirar
> para onde a mão aponta" é escolher o vetor certo; e a **orientação** do viewmodel
> acompanha sozinha, porque o client DLL a deriva da view (`V_CalcGunAngle`). Só a
> **origem** precisava de um hook novo.

- [x] 🟡 **6.1** ABI v6: `mod_viewmodel_t` + `Mod_ViewModel` + as flags `RF_VIEWMODEL_WORLD`/`RF_NO_VIEWMODEL` (que desligam o hack `VIEWMODEL_DEPTH_RANGE` e permitem esconder o viewmodel). *Pronto quando:* o engine aplica origem/ângulos/flags do Mod no viewmodel. — feito; o hook é chamado **depois do `Mods_Camera`** (o `V_GetRefParams` zera as flags) e **por olho** (o `pfnCalcRefdef` reescreve o viewmodel a cada pass), então o Mod é idempotente por frame. `mod_null` e `mod_vr` recompilados; `Mods: loaded "null" (api 6)`.
- [x] 🟡 **6.2** Mod: a arma vai para a mão — origem = olho central + o **mesmo** offset de roomscale da câmera + delta da mão (rodado pelo snap) + encaixe **no referencial da arma**. *Pronto quando:* a arma segue a mão 1:1 e é ocluída pelo mundo. — o encaixe sai de `game/mods/vr_weapon.cfg` (template versionado em `mods/vr/`), com **hot-reload de ~1 s** e tabela por classe; o painel ganhou a linha `arma` com a classe do modelo em uso.
- [x] 🟢 **6.3** Mod: `VR_AIM_SOURCE=head|left|right` (padrão `right`) — a mira passa a ser o eixo da arma. *Pronto quando:* atirar numa parede e o furo aparecer onde a arma aponta. — implementado; falta o headset (6.6).
- [x] 🟢 **6.4** Mod: a locomoção continua na cabeça mesmo com a mira na mão — o stick é pré-rotacionado por (mira − cabeça), que é a correção exata para o canal único de ângulos. Efeito colateral tratado: com o pitch do `usercmd` virando o da arma, o `pmove` escala o andar por `cos(pitch)`; `VR_MOVE_PITCH_COMP=1` compensa (padrão **desligado** = idêntico ao jogo). *Pronto quando:* andar com a cabeça virada segue a cabeça, não a arma.
- [x] 🟢 **6.5** Validação sem headset: ABI v6 carrega os dois mods, o `vr_weapon.cfg` é lido e **relido ao vivo** (editado com o jogo rodando, o log mostrou `global 0 0 0` → `1 2 3`), e o modo flat não muda (com o VR inativo o Mod devolve a struct sem tocar em nada, então as flags ficam em 0 e o `R_DrawViewModel` executa o mesmo caminho de antes). *Pronto quando:* sem headset, nada quebra e o caminho novo é observável.
- [ ] 🟡 **6.6** Validação com headset, em dois checkpoints de propósito: **(a)** `VR_AIM_SOURCE=head` — a arma na mão com a mira ainda pelo olhar, que é onde se calibra o encaixe sozinho; **(b)** `VR_AIM_SOURCE=right` — **atirar numa parede e o furo aparecer onde a arma aponta**. *Pronto quando:* o furo acompanha a arma, girar (snap + físico) não mexe na arma em relação à mão, andar segue a cabeça, encostar na parede esconde a arma, e `head` restaura o comportamento da Fase 3.
- [x] 🟢 **6.7a Viewmodel próprio (classe pistol)** — o jogo para de desenhar o viewmodel
  de 1999 e o mod desenha braços (WRAD ARMS, CC0) + pistola (Quaternius, CC0), com skin
  na CPU, **IK de dois ossos** nos dois braços até os controles, arma no osso `socket.r`
  e recuo procedural no gatilho. Oclusão pelo mundo vem de graça (o FBO do olho já tinha
  profundidade). Só a classe `pistol`; flat intocado. Doc: `docs/HANDS.md`; assets e
  licenças: `CREDITS.md` + `mods/vr/assets/SOURCES.md`. Verificado sem headset por dump
  (`VR_HANDS_TEST=1` + `VR_PANEL_DUMP`): carga, skin, projeção e a IK com poses sintéticas.
  *Falta calibrar com o headset* (escala fina e orientação da arma no soco, no `vr_hands.cfg`).
- [ ] 🟢 **6.7b Dívida registrada (fora desta etapa)**: duas mãos no rifle, roll do pulso na arma, recarga/pino por gesto, holster, buy menu no VR, haptics, animação de recarga e demais classes de arma. O recuo (`punchangle`) continua desviando o tiro e **não** empurra a câmera — bom contra enjoo, mas tira o retorno visual, que terá de vir da arma.
- [x] 🟡 **6.8** Ajuste fino do IPD pelo teclado (sintoma: "perto é difícil de fundir, longe parece certo" — clássico de separação entre as câmeras grande demais, porque a disparidade do que está longe é minúscula). ABI **v7**: `Mod_Key` + `MOD_CAP_KEYS` — o Mod vê a tecla **antes de todo handler do engine** e pode **consumi-la**, o que é o que permite uma UI de calibração sem a tecla vazar para o jogo. No `mod_vr`: F9/Home/End liga o modo; `-`/`=` = ±1,0 mm, `,`/`.` = ±0,1 mm, `0` zera; o ajuste é um **delta** sobre a base (runtime ou `VR_IPD`), aplicado **escalando** o vetor medido entre os olhos (preserva a direção, inclusive o cant); o número aparece **escrito nos dois olhos** (disparidade zero = lê-se dentro do headset), no painel 2D (`IPD 74.6 mm = base 68.3 (runtime) +6.3 | posicao +63`) e no log, e o delta persiste em `game/mods/vr_ipd.cfg`. *Pronto quando:* o ajuste muda a imagem de forma monotônica, o valor sobrevive ao restart e `0` volta à base. — implementado e validado sem headset (ABI v7 carrega; teclas injetadas na janela chegaram ao hook, o log registrou cada passo e o `.cfg` foi gravado).
  - **Resultado do dono no headset (12/10):** o melhor ficou **no piso do ajuste** (o `-40,0 mm` de então → 27,3 mm), e ele quis descer mais. A trava era **minha** (`VR_IPD_DELTA_MAX_MM 40`), não física → o limite passou a ser o **IPD final** em **[0, 110] mm** (0 = as câmeras coincidem, imagem mono; abaixo de 0 os olhos trocam de lado e o cérebro não funde).
  - **Calibração final do dono (12/10, com a faixa nova):** ele desceu de 1 em 1 mm até o **piso** — o `mods/vr_ipd.cfg` fechou em **`-67,2 mm` = IPD final 0,0 mm (mono)**, e é ali que o par funde melhor ("quanto menos, melhor" até o fim da faixa). O padrão compilado passou a ser esse (`VR_IPD_DELTA_DEFAULT_MM -67.2`, sobrescrito por `VR_IPD_ADJUST` e depois pelo `.cfg`). Como o gosto foi até o fim da faixa, o que sobra para investigar **não encolhe junto com o IPD**: desalinhamento vertical/rotacional do par (o cant do Quest 3) e o near plane fixo do engine (4 u ≈ 10 cm, que o roomscale deixa alcançar).

- [x] 🔴 **6.9** **Bug de culling na periferia** ("o mapa renderiza errado nas beiradas, sempre foi assim"): o engine escolhia o que submeter com uma frustum **simétrica** montada de `fov_x`/`fov_y` (`GL_FrustumInitProj`), e o mod publicava ali o **meio-ângulo** (54°/55°) enquanto os campos do engine são **abertura total** — a janela de culling saiu com **metade** da abertura da imagem. `R_CullSurface` só descarta superfície com a **caixa inteira** fora, então superfície grande que atravessa o centro continuava desenhada por inteiro e o defeito parecia "90% ok"; o que sumia era o que vive **só** na faixa externa (paredes distantes, o céu — cullado antes do teste de `SURF_DRAWSKY` (`gl_rsurf.c:3530`) —, água, partículas, feixes), deixando ver o que estava atrás. **Solução ideal:** `GL_FrustumInitProjAsym` (frustum pelos quatro ângulos de `fov_asym`, a MESMA região que o render) chamada em `R_SetupFrustum` quando `RF_FOV_ASYM`, e o mod passou a publicar `fov_x = right − left`, `fov_y = up − down` (unidade certa). *Pronto quando:* nenhuma direção visível é descartada e o flat não muda. — validado sem headset: a função nova coincide com a do engine em ângulos simétricos (0 divergências em 5 pares de FOV, incluindo 170×120) e, no olho esquerdo do Quest 3, a frustum antiga descartava **6.475 das 9.500 direções visíveis** contra **0** da nova; o flat renderiza igual (frame dump conferido). **Falta o olho do dono** para confirmar que a periferia ficou inteira.

**Marcador da fase:** partida com a arma na mão — *mirar com a mão, acertar onde a arma aponta, andar pela cabeça.*

---

## 4. Critérios de aceite do MVP (checklist de demo)

- [ ] Do menu do GNOME → jogo estereoscópico no Quest 3 via WiVRn, com um comando.
- [ ] Olhar 1:1 com a cabeça; andar, virar (snap), agachar e pular pelos controles.
- [ ] Atirar e acertar com mira pela cabeça.
- [ ] Comprar armas no buy menu com o ponteiro laser, dentro do óculos.
- [ ] Partida vs YAPB completa no servidor dedicado local, sem alteração no servidor.
- [ ] Modo flat intacto (baseline idêntico).
- [ ] Um segundo Mod (template `null`) carrega pela mesma ABI, provando a arquitetura.

---

## 5. Riscos e mitigações

| Risco | Impacto | Mitigação |
|---|---|---|
| Projeção assimétrica do OpenXR vs frustum simétrico do pipeline GoldSrc | distorção/clip nas bordas | MVP aceita FOV simétrico com margem (T 2.5); projeção por olho como upgrade |
| Performance: GL antigo a 90 Hz + encode WiVRn | jitter, reprojeto, enjoo | `vr_resolution_scale` (T 2.7), foveação condicional (T 2.8), pacing por `predictedDisplayTime` |
| Latência/estabilidade do Wi-Fi | enjoo, desconexões | 5/6 GHz e AP próximo; risco queimado na Fase 0 com app de teste |
| Suposições desktop no engine (mouse grab, janela única, vsync) | input quebrado em VR | Mod como "display owner" desliga paths desktop (T 2.4) |
| `buildnum` do engine vs `MIN_XASH_VERSION 4190` do cs16-client | `Host_Error` no cliente VR | branch `vr-mod` mantém versionamento ≥ master (já causou crash antes) |
| Combo WiVRn + GL + Fedora inesperado | bloqueio na Fase 2 | hello_xr isolado (T 0.2) valida antes de tocar no engine |
| Enjoo em FPS rápido (movimento do CS) | usabilidade | snap turn default, walk com grip, documentação de conforto (T 5.5); vinheta fica pro pós-MVP |
| GPL se distribuir binários | obrigação legal | xash3d-fwgs (GPL-3.0) + cs16-client (GPL-2.0): fonte do Mod aberto se distribuir; uso local sem obrigação |

---

## 6. Decisões abertas (resolver com protótipo na mão, não no papel)

1. Snap turn: rotação do reference space vs somar em viewangles — testar drift/recenter (T 3.3 decide).
2. FOV simétrico com margem já basta para o MVP, ou a projeção assimétrica é obrigatória desde o início (T 2.5 decide).
3. Painel do HUD: default "segue a cabeça" ou "fixo no mundo" — oferecer ambos, decidir default em teste (T 4.2).
4. Binding final do grip: walk (locomoção) vs secondary fire (T 3.2 vs 3.4 — prototypes decidem).
5. cs16-client precisará de branch própria? Hipótese: não — hooks no engine bastam para o MVP (reavaliar em T 4.1).

---

## 7. Direção pós-MVP (contexto da arquitetura, não escopo atual)

- **Mod v2 — arma 6DoF**: poses `aim` por controle → viewmodel e mira desacoplados do corpo (usa `Mod_Camera`/`Mod_Input` já existentes; primeiro ajuste real no cs16-client). **Etapa 1 feita na Fase 6** (arma na mão + mira pela mão, no cliente); o que resta aqui é o de 6.7: duas mãos, mãos visíveis e gestos.
- **Mãos/corpo visíveis**, áudio espacial, passthrough, vinheta de conforto.
- **Outros Mods na mesma ABI**: HD pack, gameplay overhaul, acessibilidade — fundação pronta na Fase 1, sem novo patch de engine.

---

## 8. Licenças

- `xash3d-fwgs`: GPL-3.0 · `cs16-client`: GPL-2.0 → o Mod VR, por carregar junto, herda as obrigações GPL **se distribuído em binário** (uso interno/local: nenhuma obrigação).
- ReHLDS/ReGameDLL (MIT): o projeto VR não os toca; a **trilha de bots** (§9) acrescenta um módulo próprio (`cs_bot_advisor`) ao ReGameDLL, sob a mesma licença do upstream (MIT) — ver `docs/BOTBRAIN.md`.
- Conteúdo Valve (`cstrike/`, `valve/`): permanece fora de qualquer redistribuição.

---

## 9. Trilha paralela — bots com "cérebro lento" (Jev / TypeSafe)

> **Status: V1 feita e verificada (2026-10-13), só de_dust2/defuse.** Não faz parte
> do MVP de VR; usa a mesma infraestrutura de servidor e vale igual para VR e
> flat, porque bots vivem no servidor. Documento completo: `docs/BOTBRAIN.md`.

**Forma:** o CCSBot do ReGameDLL segue como músculo (nav, visão, mira, combate);
um sidecar Python (`tools/botbrain/`) pergunta ao modelo **Jev** (TypeSafe) qual é
o plano *do time* e devolve por arquivo; o módulo `cs_bot_advisor` (dentro do
`cs.so`) aplica nos bots. O modelo nunca está no caminho crítico — o transporte é
um mailbox em disco.

**Decisões de forma (por que não "LLM por tique"):** bots rodam a 10–30 Hz e o
Jev faz 1 request por decisão em centenas de ms; então o Jev decide **onde/quando
o time compromete** (no freeze e em eventos) e os algoritmos jogam. Uma decisão
por time por momento, cache por assinatura grosseira (economia/placar/fase/clutch)
e fallback total para o comportamento stock.

**Intents (conjunto fechado):** T `RUSH_B|RUSH_A_LONG|RUSH_A_SHORT|SPLIT_A|`
`HOLD_AFTER_PLANT|ECO_SAVE|DEFAULT`; CT `STACK_A|STACK_B|RETAKE_A|RETAKE_B|`
`ECO_SAVE|DEFAULT`. O mapa intent→comportamento usa só os centros de zona do
próprio mapa (nenhuma coordenada de dust2 no código).

**Verificado:** bots jogam dust2 (nav gerado, 4v4, plantam/defusam); `RUSH_B`
planta em B e `RUSH_A_LONG` em A; o laço observe→Jev→plano→aplicado aparece no
log do servidor; e as decisões variam com o estado (postplant→`RETAKE_*`/
`HOLD_AFTER_PLANT`; inimigo repetindo B→`STACK_A`).

**Fora da V1:** outros mapas, Jev por bot, política aprendida/destilação, chatter
gerado, micro-gestão de compra, transporte por socket, UI.

