# CS 1.6 open-source — ambiente completo (Fedora Linux)

Ambiente com as quatro peças do ecossistema open-source do Counter-Strike 1.6,
compiladas do código-fonte e rodando na mesma máquina:

| Parte              | Projeto                                       | Onde está aqui            |
|--------------------|-----------------------------------------------|---------------------------|
| Engine dedicada    | [ReHLDS](https://github.com/dreamstalker/rehlds) (3.15.0.896, `550f2d6`) | `src/ReHLDS` → `server/`  |
| Lógica do jogo     | [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) (5.30.0.1-dev, `4a50c42`) | `src/ReGameDLL_CS` → `server/cstrike/dlls/cs.so` |
| Engine do jogo     | [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) (master `590869d`) | `src/xash3d-fwgs` → `game/` |
| Client             | [CS16Client](https://github.com/Velaron/cs16-client) (master `6a49bcc`) | `src/cs16-client` → `game/cstrike/` |

## Estrutura

```
cs-opensource/
├── server/            # Servidor DEDICADO (tudo 32-bit, como o HLDS original)
│   ├── hlds_linux     # launcher (compilado do ReHLDS)
│   ├── engine_i486.so # engine ReHLDS (original da Valve em engine_i486.so.valve)
│   ├── valve/, cstrike/   # conteúdo base (SteamCMD, app 90 -beta steam_legacy)
│   ├── cstrike/dlls/cs.so # ReGameDLL_CS (original em cs.so.valve)
│   ├── cstrike/server.cfg # configuração do servidor
│   └── run-server.sh  # inicia o servidor (via container podman)
├── game/              # Jogo COMPLETO (cliente, 64-bit, nativo no host)
│   ├── xash3d, libxash.so, libref_gl.so, libmenu.so, filesystem_stdio.so
│   ├── valve/, cstrike/   # conteúdo base + libs do CS16Client:
│   │      cstrike/cl_dlls/client_amd64.so  (client)
│   │      cstrike/cl_dlls/menu_amd64.so    (menu principal)
│   │      cstrike/dlls/cs_amd64.so         (game dll, ReGameDLL 64-bit)
│   │      cstrike/dlls/yapb_amd64.so       (bot YAPB)
│   └── run-cs.sh      # inicia o jogo
├── src/               # código-fonte dos 4 projetos + builds (src/*/build)
├── scripts/           # scripts auxiliares usados na montagem
└── steamcmd/…/logs    # logs do download do conteúdo
```

## Como rodar

**Servidor dedicado** (fica em `:27015`, acessível na LAN):

```bash
cd server
./run-server.sh                # de_dust2
./run-server.sh cs_office      # outro mapa
```

> O servidor roda dentro de um container podman (`docker.io/steamcmd/steamcmd`)
> porque os binários do HLDS são 32-bit e o host não tem libs multilib
> (e a instalação delas exigiria sudo). Usa `--network host`: aparece
> normalmente na sua rede. `sv_lan 1` está ligado no `server.cfg`.

**Jogo (cliente)** — roda nativo, abre uma janela:

```bash
cd game
./run-cs.sh                            # menu principal
./run-cs.sh +connect 127.0.0.1:27015   # direto no servidor local
```

Dentro do jogo: `New Game` para jogar sozinho (com bots do YAPB via console
`yapb add`), ou `Find Servers` / `+connect` para entrar no dedicado.

## Como foi montado (reproduzir do zero)

1. **Conteúdo base** (mapas, modelos, sons — proprietário da Valve, baixado
   pelo SteamCMD oficial, login anônimo):

   ```bash
   podman run --rm -v "$PWD/server":/data:Z docker.io/steamcmd/steamcmd:latest \
     +force_install_dir /data +login anonymous \
     +app_set_config 90 mod cstrike +app_update 90 -beta steam_legacy validate +quit
   ```

   ⚠️ O **beta `steam_legacy` é essencial** (recomendado no README do ReHLDS):
   ele baixa o HLDS pré-aniversário (engine 8684), cujo `libsteam_api.so` ainda
   exporta `SteamGameServer_Init` — símbolo que o ReHLDS exige e que a steam_api
   nova (build atual do app 90) removeu.

2. **ReHLDS** (32-bit): build em container com toolchain multilib
   (`gcc-multilib` numa imagem Ubuntu, ou `glibc-devel.i686` no Fedora):

   ```bash
   cd src/ReHLDS && ./build.sh -j$(nproc) -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_POLICY_VERSION_MINIMUM=3.5   # CMake 4.x exige esse flag
   cp build/rehlds/engine_i486.so       ../../server/
   cp build/rehlds/dedicated/hlds_linux ../../server/
   ```

3. **ReGameDLL_CS** (32-bit): mesmo container; `build.sh` com os mesmos flags;
   copiar `build/regamedll/cs.so` → `server/cstrike/dlls/cs.so`.

4. **Xash3D FWGS** (64-bit): `./waf configure -T release -8` (o `-8` é o modo
   64-bit; precisa de `SDL2-devel`, `freetype-devel` **e `git`** — sem git o
   waf não computa o buildnum, e o CS16Client exige build ≥ 4190).
   Instalar em `game/`: `xash3d libxash.so libref_gl.so libmenu.so
   filesystem_stdio.so` + `valve/extras.pk3`.

5. **CS16Client** (64-bit): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   -DCMAKE_POLICY_VERSION_MINIMUM=3.5` (precisa de `libfontconfig-dev`).
   Copiar para `game/cstrike/`:
   `cl_dlls/client_amd64.so`, `cl_dlls/menu_amd64.so`, `dlls/cs_amd64.so`,
   `dlls/yapb_amd64.so`, `extras.pk3`.

6. **Rebuilds rápidos (scripts prontos, todos rootless via podman):**

   ```bash
   ./scripts/build-xash.sh     # Xash3D FWGS → game/           (branch vr-mod)
   ./scripts/build-cs16.sh     # CS16Client → game/cstrike/    (branch vr-mod)
   ./scripts/build-openxr.sh   # OpenXR-SDK (loader+headers) → src/openxr-sdk/install/
   ./mods/vr/build.sh          # mod_vr (OpenXR) → game/mods/
   ./mods/null/build.sh        # mod null (template) → game/mods/
   ```

   O build do Xash normaliza o soname do bzip2 com `patchelf` (a imagem
   `steamcmd:latest` linka `libbz2.so.1.0`, o Fedora só tem `libbz2.so.1`).
   Projetos VR (plano em `PLAN.md`): branches de trabalho são `vr-mod` em
   `src/xash3d-fwgs` e `src/cs16-client`; baseline dos binários flat em
   `backups/flat-baseline-*/`; arquitetura de Mods em `docs/MODS.md`.

## VR (Quest 3 via WiVRn) — Fase 3

Lançamento: item **"Counter-Strike 1.6 VR"** no menu do GNOME (chama
`scripts/run-cs-vr.sh`) — ou direto no terminal:

```bash
./scripts/run-cs-vr.sh                          # menu em VR (ou flat, se headset off)
./scripts/run-cs-vr.sh +map de_dust2            # direto num mapa (o menu não aparece no HMD)
./scripts/run-cs-vr.sh +connect 127.0.0.1:27015 # direto no dedicado
VR_DEBUG=1 VR_DEBUG_CLEAR=1 ./scripts/run-cs-vr.sh   # debug + cores por olho
```

- Requer o WiVRn (RPM já instalado) com o dashboard/server rodando e o headset
  conectado. Sem headset: o mod detecta e o jogo roda **flat** (o init do OpenXR
  roda em thread com timeout — o jogo nunca congela; se o headset conectar
  depois, a VR ativa sozinha).
- **O mod exige X11/GLX** (é a plataforma Xlib do OpenXR). Numa sessão Wayland o
  SDL abriria no driver `wayland` (EGL) e não haveria contexto GLX nenhum — por
  isso `run-cs-vr.sh` exporta `SDL_VIDEODRIVER=x11` (XWayland dá conta). Se você
  rodar o engine na mão, defina isso também; o painel mostra o driver em uso.
- Log do mod: `game/mods/mod_vr.log`. Escala de resolução: `VR_RESOLUTION_SCALE`
  (ex.: `0.8`) — o Quest 3 pede ~2476x2594 por olho, e em GPU mais fraca vale
  reduzir. Espelho do olho esquerdo aparece na janela do monitor.
- **Painel de debug (só na tela 2D, só com o mod VR):** no canto superior esquerdo do
  monitor aparece um painel com FPS e com **todo erro/aviso do carregamento do VR**
  (e só aí — o painel e o 2D do jogo desenham na janela, nunca na imagem que vai ao
  headset; o headset mostra apenas o mundo em estéreo até a Fase 4):
  `XrResult` traduzido (ex.: `-51 XR_ERROR_RUNTIME_UNAVAILABLE`), runtime OpenXR em uso,
  valor de `XR_RUNTIME_JSON`, driver de vídeo do SDL, resolução por olho, estado da
  sessão, tempo do init e a razão do fallback flat. Ele **não vai para o headset** (é
  desenhado depois do HUD/menus e fora das swapchains dos olhos) e continua aparecendo
  justamente quando o VR falha e o jogo roda flat. `VR_DEBUG_PANEL=0` esconde,
  `VR_DEBUG_PANEL_SCALE=1..3` muda a fonte, `VR_PANEL_DUMP=/tmp/quadro.ppm` salva um
  quadro (útil quando o ambiente não deixa capturar a janela, ex.: Wayland) e
  `VR_PANEL_DUMP_AT=<s>` escolhe o atraso desse dump. O log completo continua em
  `game/mods/mod_vr.log`.
- **O que aparece no headset hoje:** o *mundo* em estéreo (a partir do momento em que há
  3D para desenhar). No **menu** o headset fica preto — a UI/menus no HMD são a Fase 4
  — então entre num mapa (ex.: `./scripts/run-cs-vr.sh +map de_dust2`, ou navegue no
  monitor e vista o headset depois). O HUD/crosshair também ainda não vão para o HMD.

### Controles (Fase 3)

Andar e girar são pelos controles; **olhar é pela cabeça** (a pose do HMD sobrescreve
os ângulos do mouse). O movimento é **orientado pela cabeça**: o stick manda a direção
que você está olhando (só o yaw — olhar para cima ou para baixo não muda a velocidade).

| Entrada | Ação |
|---|---|
| Stick esquerdo | andar / strafe |
| Stick direito ↔ | snap turn de 30° |
| Stick direito ↕ | subir/descer onde o jogo usa (água, espectador) |
| Trigger direito / esquerdo | atirar / ataque secundário |
| A / B | pular / recarregar |
| X / Y | usar / placar (segurar) |
| Click do stick esquerdo | agachar |
| Grip (qualquer) | andar devagar |
| Botão menu | **recentrar** (zera o giro do snap e recalibra a altura) |
| Trigger direito **com menu aberto** | **clicar no item apontado** (o laser vira o mouse) |

- **Roomscale:** a posição real do headset move a câmera — dá para se inclinar para
  espiar. **Agachar de verdade também agacha no jogo** (a queda da cabeça vira `IN_DUCK`);
  quem joga sentado pode desligar com `VR_CROUCH_MODE=2` (só botão) ou `0`.
- Ajustes por variável de ambiente: `VR_TURN_SNAP_ANGLE` (graus), `VR_DEADZONE`,
  `VR_MOVE_SCALE`, `VR_LEFT_HANDED=1` (troca os sticks), `VR_CROUCH_MODE`,
  `VR_CROUCH_DROP` (metros), `VR_ROOMSCALE=0` (só rotação), `VR_ROOMSCALE_MAX`,
  `VR_COMFORT_VIGNETTE` (0 = desligada; escurece a periferia enquanto você anda).
- **O bloco `INPUT` do painel** mostra o perfil de interação ativo por mão — é ele que
  responde "o controle casou?". Se vier `(nenhum)`, nenhum binding pegou e não haverá
  locomoção, mesmo com o estéreo perfeito.

### Arma na mão (Fase 6)

A arma sai do meio dos olhos e vai para a **mão direita**, e a mira passa a ser o eixo
dela: **o tiro vai para onde a arma aponta**, não para onde você olha. A locomoção
continua orientada pela cabeça (andar com a cabeça virada anda na direção do olhar, não
do cano). É tudo no cliente — o servidor não sabe que existe VR.

- `VR_AIM_SOURCE=head` volta ao comportamento da Fase 3 (mira pelo olhar) sem rebuild;
  `left` move a arma e a mira para a mão esquerda. Padrão: `right`.
- **Calibrar o encaixe:** o viewmodel do CS é de 1999 e a "pega" do modelo não está no
  ponto certo, então o deslocamento fica em `game/mods/vr_weapon.cfg` — um offset **no
  referencial da arma** (em unidades GoldSrc), com tabela por classe (pistola, rifle,
  SMG, sniper, pesada, faca, granada...). **Salvar o arquivo aplica em ~1 s, com o jogo
  rodando** — não precisa reiniciar. O painel de debug mostra a classe do modelo em uso
  e qual offset está valendo, que é o que permite calibrar olhando, sem adivinhar.
  Para referência: `X` empurra o modelo ao longo do cano, `Y` para a direita, `Z` para
  cima; um valor negativo em `X` traz a arma para perto do corpo.
- A arma é **ocluída pelo mundo**: encostar na parede esconde a arma em vez de ela
  atravessar. `VR_WEAPON_WORLD_DEPTH=0` volta ao desenho por cima (o comportamento do
  jogo flat), se preferir.
- `VR_MOVE_PITCH_COMP=1` liga uma compensação para o caso de a arma apontada para o
  chão deixar o andar lento: com a mira na mão, quem escala o movimento é o pitch da
  **arma**, não o da cabeça. Padrão desligado (comportamento idêntico ao do jogo).
- Ainda **não** tem: duas mãos no rifle, mãos/braços visíveis, roll do pulso na arma,
  recarga ou pino de granada por gesto, holster, haptics nem recuo visual. O recuo do
  jogo continua desviando o tiro, mas **não** empurra a câmera (bom contra enjoo — o
  retorno de recuo terá de vir da arma, na próxima etapa).

- **Desempenho:** o relógio do VR é o do headset (`xrWaitFrame`), nunca o do monitor — por
  isso o launcher força `+gl_vsync 0` **como comando de console** (a forma `-gl_vsync 0`
  não segura: o `config.cfg` do jogador religa o vsync, e aí o loop inteiro fica preso no
  refresh do monitor — 60 fps contra um HMD de 90 Hz, que aparece como travamento e
  **áreas pretas nas partes novas do mapa ao virar a cabeça**). O Mod imprime uma linha
  `perf:` por segundo com o trabalho por quadro vs o período do HMD, então dá para ver o
  orçamento na hora. Se precisar de folga, use `VR_RESOLUTION_SCALE` (ex.: `0.8`); se
  sobrar, dá para subir (1.3–1.6 seguram 90 fps nesta máquina, com imagem mais nítida).
- **Performance do streaming (WiVRn) — onde está o gargalo de verdade:** o jogo
  entrega 90 fps com ~1,5–2,4 ms de trabalho em um quadro de 11,1 ms (RTX 4060), então
  as faixas pretas ao virar a cabeça **não** são do render: são do caminho de streaming
  (NVENC → Wi-Fi → decodificação/reprojeção no headset), que não tem conteúdo para a
  área que acabou de entrar no campo de visão. O que dá para mexer, e o que já está
  feito do lado do PC:
  - **Resolução de render**: o runtime recomenda 2476x2594/olho, mas o WiVRn codifica
    1280x1344/olho (o log do servidor mostra isso) — renderizar 3,7x mais que o
    transmitido é trabalho jogado fora. O launcher agora usa
    `VR_RESOLUTION_SCALE=0.52` (os mesmos 1280/2476), ajustável por env.
  - **Encoder** (`~/.config/wivrn/config.json`): estava em `nvenc av1 10-bit` (a
    combinação mais pesada). Agora `nvenc h265` com `bit-depth: 8`; se ainda faltar
    fôlego, `h264` (mais barato) ou `x264` (software, sugestão do próprio FAQ do WiVRn).
  - **P-state da NVIDIA**: WiVRn usa CUDA no NVENC, o que pode prender a GPU em **P2** em
    vez de P0. O Fedora traz o perfil `CudaNoStablePerfLimit` mas só o aplica a `obs` e
    `Discord`; `~/.nv/nvidia-application-profiles-rc` agora aplica o mesmo perfil ao
    `wivrn-server` (receita de `docs/nvenc.md` do WiVRn). Confira com
    `watch nvidia-smi -q -d PERFORMANCE` **jogando** — se ficar em P0, já está resolvido.
  - **No app do WiVRn (dentro do headset, não dá para setar do PC)**: baixar o
    **bitrate** (hoje ~24,6 Mbit/s por olho, quase 50 Mbit/s no total — é o que satura o
    Wi-Fi), testar **refresh de 80 ou 72 Hz**, e ligar **codificação foveada** se a
    versão oferecer. Também dá para subir a resolução do stream, mas aí suba o
    `VR_RESOLUTION_SCALE` na mesma proporção.
  - **Link**: o PC já está cabeado; o trecho crítico é roteador→headset. `adb` está
    instalado e o dashboard já tem `auto_connect_usb=true` — conectar por **USB** (com o
    Wi-Fi do headset desligado) é a recomendação nº 1 do FAQ do WiVRn para exatamente
    esse sintoma.
  - Como separar as causas: o app do WiVRn no headset mostra bitrate/latência ao vivo —
    se a latência dispara com o bitrate colado no teto, é o link; se o link está folgado
    e a latência sobe mesmo assim, é o encoder.
- **Geometria do estéreo:** cada olho é renderizado com o frustum **próprio** que o
  runtime declara — no Quest 3 são retângulos fora de centro e espelhados (`L54 R40` no
  esquerdo, `L40 R54` no direito), e a distância entre as câmeras vem da **posição real
  de cada olho** que o runtime informa (IPD calibrado do headset; aqui dá 68 mm, contra
  os 63 mm fixos de antes). `VR_IPD=<metros>` força um valor manual. O painel de debug
  mostra `ESTEREO ipd ... | fov esq L.. R.. dir L.. R..` para conferir sem tirar o
  headset.
- **Culling com o mesmo frustum do render (correção de 12/10):** o engine decidia o que
  *submeter a desenho* com uma frustum simétrica derivada de `fov_x`/`fov_y` — e esses
  campos são aberturas **totais**, enquanto o mod publicava o **meio-ângulo** (54° e 55°).
  Resultado: a janela de culling ficava com metade da abertura da imagem (±27° contra
  ±54°), e toda geometria que vivia **só** na faixa externa (paredes distantes, o céu —
  cullado antes do teste de `SURF_DRAWSKY` —, água, partículas) simplesmente não era
  desenhada, deixando ver o que estava atrás. Como o descarte é por superfície (caixa
  inteira fora), superfícies grandes que atravessam o centro continuavam inteiras: daí
  "90% certo, a periferia errada". Agora o culling usa os **quatro ângulos por olho**
  (`GL_FrustumInitProjAsym`), idêntico ao render — medido: das 9.500 direções visíveis do
  olho esquerdo, a frustum antiga descartava 6.475; a nova, nenhuma.
### Menus no headset (Fase 4: pausa, time, classe, compra)

Com um menu aberto (`esc`, `m` de time, `b` de compra, classe), o **menu aparece numa
tela parada na sua frente**, e a **mão direita vira o mouse**: um laser sai do eixo dela,
você aponta para o item e o **gatilho direito clica**. Sem menu aberto não existe painel,
laser, cursor nem clique — o jogo normal fica exatamente como era (e o `esc`/`m`/`b`
continuam abrindo os mesmos menus de sempre).

- **A tela é ancorada no mundo**, na frente e na altura dos olhos, **no instante em que o
  menu abriu** — ela não segue a cabeça: se você virar para o lado, ela fica para trás (é
  só olhar de volta). Cada abertura reancora. O **stick direito não gira** com menu aberto,
  então um toque acidental não joga a tela para trás; olhar com a cabeça continua livre.
- **Como isso funciona por dentro:** o engine captura o **desenho do menu** (o `mainui`
  desenha os menus com as funções 2D do engine) num alvo offscreen e devolve o resultado
  para a janela — então **o monitor continua mostrando o menu normalmente** —, e o Mod
  desenha esse alvo como um painel nos olhos. **Só os menus entram no painel**: o HUD do
  jogo (vida/munição/radar/mira, o ícone verde de zona de compra) e o console continuam
  desenhando **na janela**, como sempre — capturados, eles apareceriam **por cima** dos
  itens do menu dentro do headset. O ponteiro segue o caminho inverso: o laser **vira a
  posição do mouse** para os dois lugares que leem o cursor (a UI do engine e os menus do
  client DLL).
- **A tela sai na orientação certa** (não espelhada): o painel é reconstruído a partir da
  posição em que você olhava, com a "direita" do painel sendo `frente × cima` — a mesma
  convenção do engine. Um erro de sinal aqui deixava o menu espelhado (e cisalhado) em
  quase todo ângulo; a correção foi verificada projetando o canvas real com a matemática do
  Mod (`/tmp/warp_panel.py`) fora do headset.
- **Calibrar a tela** (com o jogo rodando; salvar já aplica, mesmo com o menu aberto):
  `game/mods/vr_menu.cfg` — `dist_m` (distância, padrão 1,30 m), `height_m` (altura
  física, padrão 0,95 m; **o texto cresce junto** — é a razão altura/distância que decide
  a legibilidade, e a largura segue o aspecto da janela), `yaw_off_deg` e `y_off_m` para
  um ajuste fino de enquadramento. Variáveis de ambiente equivalentes para o padrão:
  `VR_MENU_DIST`, `VR_MENU_HEIGHT`; `VR_MENU=0` desliga tudo e `VR_MENU_LASER=0` esconde
  só o laser.
- **Diagnóstico:** `VR_MENU_DEBUG=1` registra em `game/mods/mod_vr.log` os fatos que o
  engine manda (`menu aberto? canvas? tamanho do espaço 2D`) e, 1x/s, para onde o laser
  aponta. O painel de debug da tela 2D ganhou duas linhas (`menu ABERTO | canvas tx N |
  ponteiro ... | cliques N` e `painel dist ... | ancora ... | laser ...`).
- **Testar a metade do engine sem headset** (é o que valida "o clique chega ao menu"):
  `./xash3d -game cstrike -mod vr -console +hud_ptr_test 2 +map de_dust2` — o cvar
  privilegiado `hud_ptr_test` força o canvas e pilota o cursor; `2` também abre e fecha o
  menu sozinho (comandos depois de `+map` se perdem no carregamento do mapa, então não dá
  para encadear `+togglemenu`), e `3` abre o **menu de compra de verdade**
  (`showvguimenu 28`, o mesmo caminho da tecla `b`) e o deixa aberto. Com
  `HUD_CANVAS_DUMP=/tmp/x.pam` o engine grava **o conteúdo exato do painel** (PAM RGBA;
  `HUD_CANVAS_DUMP_FRAME` escolhe o frame) e imprime o histograma de alfa — é assim que se
  confere, sem headset, o que o painel mostra. Sem headset o Mod fica inativo e nada disto
  acontece.
- **Nota de bastidor (modo janela):** o engine só escreve no terminal com `-console`
  (`Con_Printf` sai cedo sem `host.allow_console`) — sem ele, um run gráfico fica mudo no
  stdout. E `+comandos` (`+map`, `+screenshot`) **só rodam se algo chamar `stuffcmds`**
  (cvars como `+fps_max 200` funcionam sempre): o arquivo `game/cstrike/userconfig.cfg`
  deste ambiente faz isso, é o que torna o `+map de_dust2` do `scripts/run-cs-vr.sh`
  funcionar de verdade.

- **Ajuste fino do IPD (pelo teclado, com o jogo rodando):** o IPD que o runtime informa
  é do *headset*, não do jogo, e é ele que decide se o par funde — **grande demais,
  objetos próximos pedem convergência excessiva e não fundem**, enquanto os distantes
  (disparidade minúscula) continuam parecendo certos. Para calibrar:

  | Tecla | O que faz |
  |---|---|
  | **F9** (ou Home/End) | liga/desliga o **modo de ajuste** |
  | `-` / `=` | aproxima / afasta as câmeras em **1,0 mm** |
  | `,` / `.` | o mesmo em **0,1 mm** (fino) |
  | `0` | zera o ajuste (volta ao valor de base) |

  Com o modo ligado essas teclas são **consumidas** (o jogo não as vê) e o valor aparece
  **escrito dentro do headset** — nos dois olhos, com disparidade zero, que é o único
  jeito de ler o número calibrando com o HMD na cabeça — e também no painel 2D:
  `IPD 74.6 mm = base 68.3 (runtime) +6.3 | posicao +63`. A **posição** (+63) é o mesmo
  ajuste em décimos de milímetro: é o número curto para anotar e reportar. Cada mudança
  vai para `game/mods/mod_vr.log` e para `game/mods/vr_ipd.cfg`, que é relido no próximo
  início — **o ajuste sobrevive ao restart** (`0` zera; `VR_IPD_HUD=0` esconde o texto no
  headset, sem tirar o valor do painel nem do log).

  O IPD final fica na faixa **0–110 mm** e **o piso 0 é física, não configuração**: com
  0 mm as duas câmeras coincidem e os dois olhos recebem a *mesma* imagem — o estéreo
  deixa de existir (imagem sem profundidade, o painel avisa `MONO (cameras coincidem)`).
  Abaixo de zero os olhos trocariam de lado e o par vira espelho, que o cérebro não funde;
  por isso não há como descer mais. O ajuste já **nasce calibrado** no valor que ficou
  melhor aqui (**−67,2 mm**, que sobre a base do Quest 3 dá **IPD 0,0 mm** — as câmeras
  coincidindo, imagem mono — foi até onde a calibração no headset pediu): `VR_IPD_ADJUST=<mm>`
  troca esse padrão e o `vr_ipd.cfg` continua tendo a palavra final.
- **Diagnóstico do estéreo:** `VR_TRACE=1` registra os primeiros frames do pipeline
  (FBO por olho, câmera, fov/roll por olho, conteúdo lido da imagem da swapchain,
  `xrEndFrame`) sem parar; sem ele, os 12 primeiros frames já saem no log. O Mod também
  avisa a cada ~5 s *onde* o pipeline parou ("ainda nao VISIBLE" ou "sem render 3D nos
  olhos") e imprime a geometria do par (IPD, eixo dos olhos, se as câmeras são
  paralelas). `VR_STEREO_DUMP=pref` grava `pref-<n>-eye0.ppm` e `pref-<n>-eye1.ppm` do
  **mesmo quadro**, que é o que permite medir a disparidade do par fora do headset.
- **Menus no headset** já funcionam (Fase 4, recorte): `esc`/`m`/`b` abrem o menu numa
  tela parada na sua frente e o ponteiro laser da mão direita clica nos itens (ver
  §"Menus no headset"). O painel mostra **só o menu**: o HUD do jogo (vida/munição/radar/
  mira), o placar e o console continuam desenhando **só no monitor** — o painel é a tela do
  menu, não um espelho da tela do jogo.

## Bots com IA — "cérebro lento" (Jev / TypeSafe)

O servidor dedicado roda bots de CS (ReGameDLL/CCSBot) **guiados por rodada por
um modelo Jev (TypeSafe)**, via um sidecar Python local. O Jev escolhe o plano
*do time* (rushar A/B, stackar, retomar, economizar) e os bots executam com os
algoritmos de sempre. Vale igual para VR e flat — bots vivem no servidor.

```bash
scripts/run-botbrain.sh    # sidecar + servidor (de_dust2), 4v4
```

Documento completo (arquitetura, protocolo, cvars, como depurar, o que foi
verificado): **[docs/BOTBRAIN.md](docs/BOTBRAIN.md)**.

> Se o bot não andar: o `bot_enable 1` precisa estar no
> `server/cstrike/game_init.cfg`, o `BotProfile.db` no `server/cstrike/`, e o
> `de_dust2.nav` gerado uma vez (`bot_nav_analyze` + `bot_nav_save`). Detalhes na
> §7 do doc acima.

## Observações e limitações

- **Conteúdo**: os mapas/modelos/sons vêm do HLDS da Valve via SteamCMD
  (login anônimo, permitido pela Valve para servidores dedicados). Funciona
  para jogar; para 100% do conteúdo client-side (ex.: HD models, alguns
  arquivos de UI), copie `valve/` e `cstrike/` de uma instalação sua do
  CS 1.6 no Steam por cima do `game/`.
- **Backups**: `engine_i486.so.valve`, `hlds_linux.valve` e
  `cstrike/dlls/cs.so.valve` são os binários originais da Valve —
  restaurar eles = voltar ao HLDS puro.
- **SELinux**: o `run-server.sh` usa `--security-opt label=disable` para não
  relabelar os arquivos do servidor. O download inicial usou `:Z` (relabelou
  `server/` para uso em container — inofensivo para uso normal no host).
- **RCON**: troque `rcon_password` em `server/cstrike/server.cfg`.
- Para rebuilds, todos os passos exatos estão na seção acima; os binários
  compilados continuam em `src/*/build`.
