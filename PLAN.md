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

- [ ] 🟡 **2.1** Esqueleto do mod_vr: instance/session OpenXR contra o WiVRn (via `XR_RUNTIME_JSON`), reference space `LOCAL`, spaces de head e hands. *Pronto quando:* log confirma sessão criada e poses chegando (sem render).
- [ ] 🟡 **2.2** Frame loop OpenXR (`xrWaitFrame` → `xrBeginFrame` → `xrLocateViews` → `xrEndFrame`) acoplado a `Mod_FramePre/Post`, com pacing pelo `predictedDisplayTime`. *Pronto quando:* loop estável no refresh do HMD (72/90 Hz) com o jogo rodando flat ao lado.
- [ ] 🔴 **2.3** Swapchains GL por olho (`XR_KHR_opengl_enable`): criar swapchains, FBO por image, bind no hook de câmera. *Pronto quando:* cor de clear distinta por olho visível no headset (estéreo "burro").
- [ ] 🔴 **2.4** Camera override + render duplo: origem/ângulos do HMD → view do engine, cena desenhada **2× por frame** (um por olho), offsets apenas de render (física/prediction intocadas). Desligar paths desktop: mouse grab/warp, vsync de janela, cursor de software. *Pronto quando:* em pé no mapa, olhar 1:1 ao redor; jogo ainda controlável por teclado/mouse; sem "mouse fight".
- [ ] 🔴 **2.5** Projeção por olho: plugar a matriz OpenXR (assimétrica) no renderer via hook/patch mínimo no `ref_gl`. Fallback do MVP documentado: FOV simétrico = max por eixo, com margem. *Pronto quando:* estéreo confortável, sem clipping grave nas bordas.
- [ ] 🟡 **2.6** Submit + mirror: `xrEndFrame` com projection layers; copiar olho esquerdo para a janela SDL (debug no monitor). *Pronto quando:* cena estéreo no headset + espelho no monitor.
- [ ] 🟢 **2.7** `vr_resolution_scale` (0.5–1.5) e medição de fps no HMD. *Pronto quando:* escala default escolhida sustenta refresh estável na RTX 4060 no de_dust2.
- [ ] 🟡 **2.8** (Condicional) Foveação fixa via extensão FB, se o WiVRn expuser. *Pronto quando:* cvar `vr_foveation` com ganho mensurável de margem de performance.

**Marcador da fase (maior risco técnico queimado):** *dust2 em estéreo, olhando ao redor, jogo 100% funcional por teclado/mouse.*

### Fase 3 — Input VR (controles Quest)

- [ ] 🟢 **3.1** Action set OpenXR: thumbsticks, trigger, grip, A/B/X/Y, menu, haptic em `/user/hand/*`; poses `aim`. *Pronto quando:* overlay de debug mostra estados dos controles ao vivo.
- [ ] 🟡 **3.2** Locomoção: stick esquerdo → forward/side **relativo ao yaw da cabeça**, com deadzone e walk (velocidade reduzida) segurando grip. *Pronto quando:* andar/strafe no jogo só com o controle.
- [ ] 🟡 **3.3** Snap turn: stick direito horizontal → passo de 30° (cvar `vr_snap_turn`, 15/30/45), via rotação do reference space + viewangles. *Pronto quando:* virar em passos sem drift e sem precisar de recenter manual constante.
- [ ] 🟡 **3.4** Botões → ações do jogo: trigger = fogo, A = pulo, B = reload, X = use/E, Y = lanterna, click stick esq. = agachar (toggle), menu = ESC virtual — injetados pela rota de input do engine. *Pronto quando:* todas as ações executam na partida.
- [ ] 🟢 **3.5** Mira pela cabeça: viewangles derivam do HMD (yaw do corpo segue a cabeça), crosshair central. *Pronto quando:* acertar alvos fixos e móveis de forma consistente.
- [ ] 🟢 **3.6** (Opcional, cortável) Haptic curto ao atirar. *Pronto quando:* vibração perceptível por tiro, sem atrapalhar input.

**Marcador da fase:** partida jogável de ponta a ponta **sem teclado nem mouse** (spawn → andar → atirar → recarregar).

### Fase 4 — HUD e menus em VR

- [ ] 🔴 **4.1** Camada de captura 2D: hook pós-HUD redireciona todo desenho 2D (HUD do cs16-client, menus do menu_amd64, console, texto do engine) para um FBO offscreen (canvas virtual, ex. 1920×1080). *Pronto quando:* canvas contém HUD e menus renderizados (verificável via dump de textura).
- [ ] 🟡 **4.2** Painel virtual: canvas → textura de um quad no mundo, ancorado ao HMD com follow suave de yaw; cvars `vr_hud_dist`/`vr_hud_scale`; modos "painel" (segue a cabeça) e "TV" (fixo no mundo). *Pronto quando:* HUD e menus nítidos e legíveis dentro do óculos.
- [ ] 🟡 **4.3** Ponteiro laser: raio da pose `aim` do controle intersecta o quad → mapeia para coordenadas 2D do canvas → injeta mousemove/click. *Pronto quando:* navegar menu principal, **buy menu** e console usando só o controle.
- [ ] 🟢 **4.4** Scoreboard e chat: confirmar que fluem pelo mesmo canvas (client HUD + texto do engine). *Pronto quando:* legíveis em partida.
- [ ] 🟢 **4.5** Overlay de debug do Mod (fps do HMD, poses, tempo de submit) atrás de `vr_debug 1`. *Pronto quando:* diagnóstico possível sem sair do óculos.

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

- **Mod v2 — arma 6DoF**: poses `aim` por controle → viewmodel e mira desacoplados do corpo (usa `Mod_Camera`/`Mod_Input` já existentes; primeiro ajuste real no cs16-client).
- **Mãos/corpo visíveis**, áudio espacial, passthrough, vinheta de conforto.
- **Outros Mods na mesma ABI**: HD pack, gameplay overhaul, acessibilidade — fundação pronta na Fase 1, sem novo patch de engine.

---

## 8. Licenças

- `xash3d-fwgs`: GPL-3.0 · `cs16-client`: GPL-2.0 → o Mod VR, por carregar junto, herda as obrigações GPL **se distribuído em binário** (uso interno/local: nenhuma obrigação).
- ReHLDS/ReGameDLL (MIT): não são tocados pelo projeto VR.
- Conteúdo Valve (`cstrike/`, `valve/`): permanece fora de qualquer redistribuição.
