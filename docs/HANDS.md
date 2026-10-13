# Mãos e arma de verdade (viewmodel próprio da VR) — `vr_hands`

O viewmodel de 1999 do CS **deixa de aparecer na classe `pistol`** e no lugar entram os
braços do **WRAD ARMS** com skinning e uma pistola do **Quaternius**, que seguem as mãos:
os dois braços fazem IK contra os controles, os dedos fecham no cabo, a arma pode ir para
o **coldre da barriga** ou para o **chão**, e dá para segurá-la com **as duas mãos**.

Vale só onde há VR: no jogo flat (monitor) o viewmodel continua o de sempre, intacto.

---

## O que a ABI do Mod permite (e o que não permite)

A ABI (v8, `engine/common/mod_api.h`) entrega ao Mod: `Printf`, `DoubleTime`,
`engine_version` (`:69-75`) e nada mais. **Não há** carregador de modelo/textura, canal de
comando, entidade nem física. O que existe e é usado aqui:

| Peça | Onde | Para que serve |
|---|---|---|
| `mod_viewmodel_t.hide` | `mod_api.h:178` | o jogo para de desenhar a arma dele |
| `mod_input_sample_t` | `mod_api.h:119` | mira absoluta + `MOD_BTN_*` (é o gate do gatilho) |
| `mod_view_t.fov_asym` | `mod_api.h:91` | frustum assimétrico do olho — **em radianos** |
| FBO do olho com `GL_DEPTH_COMPONENT24` | `mod_vr.cpp:2187` | o mundo oclui os braços de graça |
| `vr_eye_cam[e]` | `mod_vr.cpp:755` | origem/ângulos/fov/tamanho por olho |
| `in_hand_g[]` / `in_hand_pose[]` | `mod_vr.cpp:940` | posição e pose dos controles |

Consequências que moldaram o desenho:

- **A arma do servidor é a arma do CS.** Este subsistema é a camada visual e de intenção:
  ele decide o que se vê, para onde se mira e se o gatilho vale. Ele **não** troca, saca ou
  descarta arma no inventário — a ABI não tem canal para isso.
- **Não há raycast**, então o "chão" da arma largada é o chão do próprio jogador
  (`player_origin.z − altura dos olhos`), que já acompanha escada/caixa.
- **A origem do projétil continua no olho**: a ABI não deixa mover a origem do tiro, só a
  direção (`viewangles_abs`). Mira pelo cano, saída pelo olho.

---

## Arquivos

| Arquivo | Papel |
|---|---|
| `mods/vr/vr_hands.h` | interface: `update` (1x/frame) / `draw` (por olho) / `sheet` |
| `mods/vr/vr_hands.cpp` | o subsistema: carga, encaixe, IK, dedos, estados, desenho, folhas |
| `mods/vr/vr_hands.cfg` | calibração (hot-reload: **salvar já aplica**) |
| `mods/vr/assets/` | `arms.glb` + `Pistol_1.obj/.mtl` (CC0; ver `assets/SOURCES.md`) |
| `mods/vr/tools/glb_inspect.py` | relatório do rig (ossos, skin, bbox) |
| `mods/vr/tools/skin_probe.cpp` | prova o skinning fora do GL |
| `mods/vr/tools/ppm2png.py` | converte o dump PPM do mod em PNG (só stdlib) |

## Fluxo por frame

```
VR_FramePre ──► vr_hands_frame()          [1x por frame, DEPOIS do xrSyncActions]
                 ├─ vr_hands_body_fill()  corpo: olho + roomscale, yaw do tronco, chão
                 └─ vr_hands_update()     estado da arma, IK, dedos, encaixe, skin
VR_Input    ◄── vr_hands_state             mira do CANO + gate do MOD_BTN_ATTACK
VR_ViewModel ──► vr_hands_covers_model()   vm->hide = 1 (só na classe coberta e só em VR)
VR_AfterEyes ──► vr_hands_draw(cam)        por olho: só desenha a pose já resolvida
```

O `update` roda **uma vez por frame** (não por olho): os dois olhos trocam a matriz e a
arma/braço ficam idênticos nos dois — senão a estereoscopia do que está na mão sai errada.
No modo de teste (sem headset) o `update` é chamado do `VR_AfterEyes`, porque fora de
sessão não existe `FramePre` de VR.

---

## O encaixe é resolvido, não chutado

Esta é a parte que mudou de verdade na V2. O que existia antes era um multiplicador mágico
e um quaternion de osso — foi assim que a pistola ficou **de lado, para trás e 4,2× maior**.

**1. Eixos do asset, declarados.** Cada arquivo tem a sua convenção (`arms_axes`,
`weapon_axes` no cfg): `-Z +Y +X` = frente −Z, cima +Y, direita +X (glTF/WRAD);
`+X +Y +Z` (OBJ/Quaternius, comprimento no +X). Aplicar a mesma conversão nos dois punha a
pistola atravessada.

**2. Referencial da MÃO.** A arma é montada no referencial da mão (frente = cano, esquerda,
cima), derivado do próprio rig no load (ver `assets/SOURCES.md`): a palma sai da média das
bases dos dedos, e a rotação da mão segue a **pose do controle** — sem isso o punho ficava
na pose de repouso do rig e a arma não tinha como entrar na mão.

**3. Empunhadura ancorada na palma.** `weapon_grip` é um ponto **dentro do modelo** (medido:
centroide do cabo da `Pistol_1`, `−0,10 −0,23 0`). Ele cai exatamente no centro da palma —
o relatório imprime `empunhadura a 0.00 un da palma` quando isso está certo.

**4. Escala pelo comprimento REAL.** `weapon_length_cm` (19) ÷ o comprimento medido do
modelo (1,82) = **4,11 un/un** → a pistola sai com **7,48 unidades GoldSrc = 19,0 cm**.
Nenhum asset precisa de número mágico, e o bug da escala dobrada (o mundo do osso `socket`
já carregava `scale_arms` e o multiplicador entrava por cima) morreu na raiz.

**5. Resíduos com sentido.** Sobram dois ajustes de gosto, ambos **no referencial em que a
arma é montada**: `weapon_off` (unidades GoldSrc: frente/esquerda/cima) e `weapon_euler`
(graus: `pitch` = **boca para cima**, `yaw`, `roll`). O `holster_euler` usa a mesma
convenção. Com os padrões, os dois ficam em zero e o encaixe já está certo.

**6. A mesma conta para os três lugares.** `mount_weapon()` monta a arma na mão, no coldre e
no chão — só muda o referencial. Não há três cópias da matemática para divergirem.

## Dedos que fecham

O rig tem **30 ossos de dedo** (`finger_{pinky,ring,middle,index,thumb}{1,2,3}.{l,r}`) e o
eixo de flexão de cada falange é **derivado do repouso**, sem tabela por asset: no espaço
local do osso o comprimento corre em `+Y`, então girar em torno de
`cross(Y, direção-para-a-palma)` move a ponta do dedo **na direção da palma**. Vale igual
para as duas mãos e para o polegar. O quanto fecha vem do cfg (`curl_max` por falange e um
peso por dedo), e a mão que **segura** a arma soma um `curl_fist`: o punho fecha no cabo
mesmo com o analógico em 0,6.

## Estados: mão, coldre, chão e duas mãos

```
   NA MÃO ──── grip solto longe da barriga ───► NO CHÃO ──── grip perto ────► NA MÃO
      │                                            ▲
      └── grip solto com a mão na barriga ──► NO COLDRE ◄──── grip perto ────┘
```

- **NA MÃO** é o estado inicial. A **mão de trás** (quem segura o cabo) é a que pegou; a
  outra é apoio.
- **Duas mãos** quando os dois grips estão apertados **e** a mão de apoio está dentro de
  `two_hand_radius` da âncora de apoio. O cano passa a ser a direção
  `mão_de_apoio → mão_de_trás` misturada com a frente da mão de trás (`two_hand_blend`), a
  mão de trás gira **junto** com a arma, e o IK da mão de apoio mira a **arma** (a arma
  conduz a mão, que é o padrão dos jogos).
- **Soltar** só derruba a arma quando **ninguém** a segura: de duas mãos, largar a de trás
  não a derruba.
- **Pegar** é por borda do grip + raio (`grab_radius`), com histerese (`grab_hyst`); vale
  com **qualquer** das mãos, e a arma troca de mão de trás.
- **Chão**: a arma cai (gravidade real, `drop_gravity` = 9,8 m/s²) até o plano do chão do
  jogador e deita (o cano fica horizontal).
- **Coldre**: fica na barriga, boca para baixo (`holster_euler` = −70° → medido: o cano faz
  **−0,94 com a vertical do corpo**). A âncora é do **corpo**: `player_origin` + roomscale,
  com o yaw do tronco — que pode atrasar a cabeça (`VR_BODY_YAW_LAG`), senão olhar para o
  lado gira o coldre junto.

## Integração com o jogo

- **Mira pelo cano** (`VR_AIM_FROM_WEAPON`, ligado): a direção da arma na mão é o que vai ao
  servidor. É o que faz o grip de duas mãos e o saque com qualquer mão concordarem com o que
  se vê. Desligar volta a mirar pela mão/cabeça (`VR_AIM_SOURCE`).
- **Gate do gatilho** (`enforce_armed` no cfg, ligado): fora da mão o gatilho não atira — e
  isso vale **só para a classe que este subsistema desenha**; com o rifle equipado, nada
  muda. O recuo visual só dispara com a arma na mão.
- **Recuo visual** no referencial da arma (para trás no cano + boca para cima), em unidades
  GoldSrc do cfg. O recuo que desvia o tiro continua sendo o do jogo.

---

## Configuração (`mods/vr/vr_hands.cfg`)

Salvar o arquivo **já aplica**: o mod relê por mtime (~1×/s) e recarrega os assets — dá para
ajustar encaixe, escala, eixos e a folha de contato sem reiniciar o jogo.

| Chave | Padrão | O que é |
|---|---|---|
| `enable` | 1 | 0 = volta ao viewmodel do jogo |
| `arms_glb` / `arms_tex` / `weapon_obj` | assets do WRAD/Quaternius | caminhos (relativos a `game/`) |
| `arms_axes` / `weapon_axes` | `-Z +Y +X` / `+X +Y +Z` | convenção DENTRO de cada arquivo |
| `scale_arms` | 4,2 | rig → unidades GoldSrc |
| `weapon_length_cm` | 19 | comprimento real alvo (a escala sai daqui) |
| `weapon_scale_res` | 1 | resíduo multiplicativo |
| `weapon_grip` | −0,10 −0,23 0 | ponto de empunhadura no espaço do MODELO |
| `weapon_off` / `weapon_euler` | 0 | resíduo: unidades GoldSrc / graus (pitch = boca p/ cima) |
| `support_fwd` / `support_side` / `support_up` | 1,6 / 1,4 / 0 | onde a mão de apoio vai |
| `curl_max` | 85 95 75 | flexão máxima por falange (graus) |
| `curl_pinky`…`curl_thumb` | 1,0 1,0 0,95 0,85 0,6 | peso por dedo |
| `curl_fist` | 0,75 | fechamento mínimo da mão que segura |
| `grip_th` | 0,6 | squeeze que conta como "apertado" |
| `holster_fwd/down/side` | 10 / 22 / 0 | coldre: à frente e abaixo do olho |
| `holster_euler` | −70 0 0 | pose guardada (pitch negativo = boca para baixo) |
| `holster_radius` / `grab_radius` / `grab_hyst` | 9 / 7 / 1,7 | guardar / pegar / histerese |
| `two_hand_radius` / `two_hand_blend` | 6 / 0,5 | entrar nas duas mãos / peso do cano |
| `drop_gravity` / `drop_eye_height` | 386 / 28 | queda e chão |
| `recoil_deg` / `recoil_kick` / `recoil_interval` | 7 / 0,9 / 0,09 | recuo visual |
| `enforce_armed` | 1 | fora da mão, o gatilho não atira |
| `sheet` | 0 | folha de contato (ver abaixo) |
| `synth_hands` / `synth_grip` / `synth_dist` / `synth_side` / `synth_down` | 0 / 1 / 24 / 5,5 / 9 | mãos sintéticas do diagnóstico |
| `near` / `far` / `color_gamma` | 4 / 7086 / 2,2 | casar com o ref; o `Kd` do MTL é linear |

## Diagnóstico sem headset: as folhas de contato

`VR_HANDS_TEST=1` (ou qualquer `VR_HANDS_SHEET`) desenha no **FBO da janela**, e o
`VR_PANEL_DUMP` salva o quadro. A folha é uma **grade de vistas do MESMO caminho de
desenho** — o que se vê é o que o jogo desenha. As vistas são **relativas ao corpo** (yaw do
tronco): `0` = de frente para o jogador, `90` = do lado esquerdo, `180` = por trás.

| `VR_HANDS_SHEET` | Grade | Para decidir |
|---|---|---|
| `fit` | 4×3 ângulos em volta da mão | encaixe, orientação e TAMANHO da arma na mão |
| `weapon` | 4×3, a arma sozinha + marcadores | se a empunhadura está no cabo e o eixo no cano |
| `curl` | 4 níveis de grip × 2 mãos | direção e quantidade do fechamento dos dedos |
| `states` | 2×2 | as quatro poses: na mão, duas mãos, coldre, chão |
| `walk` | 4×2 | **roteiro pela máquina de estados** (pegar/guardar/largar/2 mãos) |

No modo `weapon` os marcadores são linhas: **vermelho** = eixo do cano, **verde** = lado,
**azul** = cima da arma, **branco** = comprimento (na escala real). Eles provam as âncoras.

O mesmo desenho imprime um **relatório numérico** no log — é o que permite decidir sem olhar
a imagem:

```
maos: folha 'fit' 4x3 | escala 4.11 un/un | comprimento real 19.0 cm | estado na mao
maos: encaixe: cano x frente-da-mao = 0.0 graus | empunhadura a 0.00 un da palma | arma 7.48 un (19.0 cm)
maos: coldre: cano x cima-do-corpo = -0.94 (cfg pitch -70) | cano x frente = +0.34
```

E o modo `walk` roda a máquina de estados de verdade, passo a passo:

```
maos: passo 0: grip esq 0.00 dir 1.00 -> na mao | mao de tras dir
maos: passo 1: grip esq 0.00 dir 0.00 -> no chao | mao de tras dir
maos: passo 3: grip esq 1.00 dir 0.00 -> na mao | mao de tras esq      (pegou do chão com a esquerda)
maos: passo 4: grip esq 1.00 dir 1.00 -> na mao | mao de tras esq +2 maos
maos: passo 5: grip esq 0.00 dir 1.00 -> na mao | mao de tras esq +2 maos  (soltar a de trás não derruba)
maos: passo 6: grip esq 0.00 dir 0.00 -> no coldre | mao de tras esq   (levou à barriga)
maos: passo 7: grip esq 0.00 dir 1.00 -> na mao | mao de tras dir      (sacou de novo)
```

Receita (do `game/`, que é o cwd do xash3d — precisa de X11):

```sh
cd game
VR_HANDS_SHEET=fit VR_DEBUG_PANEL=0 VR_PANEL_DUMP=/tmp/fit.ppm VR_PANEL_DUMP_AT=12 \
  SDL_VIDEODRIVER=x11 ./xash3d -game cstrike -mod vr +map de_dust2
python3 ../mods/vr/tools/ppm2png.py /tmp/fit.ppm /tmp/fit.png
```

Outras variáveis úteis: `VR_HANDS_FAKE_MODEL=models/v_usp.mdl` (injeta o nome do viewmodel
sem arma equipada), `VR_AIM_FROM_WEAPON=0`, `VR_BODY_YAW_LAG=0.25`, `VR_WEAPON_DEBUG=1`.

## Verificado sem headset

| O que | Como | Resultado |
|---|---|---|
| Carga dos assets | log do load | 1196 tri / 872 verts / 50 ossos + 1040 tri da arma |
| Referencial da palma | log do load | 0,37 un. do rig do punho, normal para o corpo nas duas mãos |
| Encaixe na mão | folha `fit` + relatório | cano a **0,0°** da frente da mão; empunhadura a **0,00 un** da palma |
| Tamanho | folha `fit` + relatório | **7,48 un = 19,0 cm** (era ~32 un) |
| Âncoras | folha `weapon` | cruz no cabo, linha branca ao longo do cano |
| Dedos | folha `curl` | fecha na direção da palma, nos 4 níveis, **nas duas mãos** |
| Coldre | relatório + folha `states` | cano a **−0,94** da vertical do corpo (boca para baixo) |
| Estados | folha `walk` + log | **8/8** passos: pegar (esq), 2 mãos, soltar a de trás, guardar, sacar (dir) |
| Flat não regrediu | jogo sem `VR_HANDS_TEST` | nenhuma linha do subsistema no log; mod carrega normal |

### Falta validar com o headset (seu lado)

1. Tamanho e orientação da pistola na mão (é o `weapon_length_cm` e o `weapon_euler`).
2. Fechar a mão com o grip (`curl_fist` é o mínimo quando ela segura a arma).
3. Sacar do coldre e guardar (o coldre está 10 un à frente e 22 abaixo do olho).
4. Duas mãos: se o encaixe da mão de apoio precisar de ajuste, é `support_fwd/side/up`.
5. Pegar do chão e trocar de mão.
6. A mira pelo cano: se preferir a de antes, `VR_AIM_FROM_WEAPON=0`.
7. Se o coldre incomodar ao olhar para o lado, ligue `VR_BODY_YAW_LAG=0.25`.

## Limites desta versão

- Só a classe **`pistol`** (usp, glock18, p228, deagle, elite, fiveseven) e um único par
  braços+arma. As outras classes seguem exatamente como antes.
- **Sem recarga, sem gesto de pino de granada, sem haptics e sem som.** O pedido era mirar e
  atirar por movimento — e é o que está aqui.
- **O inventário é do jogo.** Guardar/largar é estado do cliente (o que se vê e se o gatilho
  vale); a arma equipada no servidor não muda. A arma largada também não sobrevive a troca
  de mapa e não tem colisão com o mundo.
- A **origem** do projétil continua no olho (a ABI não deixa movê-la) — só a direção vem do
  cano.
- O punho gira como um corpo rígido (a rotação do controle entra no osso do punho): não há
  torção de antebraço.
- Skinning na **CPU** em modo imediato: ~2.200 tri, 1×/frame. Se um dia pesar, o caminho é
  arrays de vértice, sem mudar o resto.

## Notas de engenharia (bugs que custaram tempo)

1. **`fov_asym` é em RADIANOS** (a ABI entrega assim e o `proj_matrix` aplica `tan()`).
   Guardar a tangente aplicava o `tan` duas vezes e achatava o modelo.
2. **A inversa tem de ser geral, não "rígida".** As matrizes de mundo do rig carregam a
   escala (`scale_arms`); a inversa que assume rotação ortonormal gerava locais com
   translação de centenas de unidades e a malha explodia.
3. **O skin já está em espaço de mundo.** O `cur[root]` inclui a colocação do rig, então o
   desenho dos braços entra com a matriz **identidade** — aplicar a colocação de novo
   jogava os braços para longe (a arma, que não tem skin, continuava certa).
4. **Modo imediato com *client arrays* ligados é descartado.** O `ref_gl` desenha o mundo
   com vertex arrays; desligar tudo antes do `glBegin` evita vértices descartados.
5. **"No VR não apareceu nada" — o gate escondia e o gate desenhava discordavam.** O bloco
   que grava o nome do viewmodel (`vr_wpn_model`) ficava *depois* do `return` que esconde o
   viewmodel do jogo: o jogo escondia a arma dele e o `VR_AfterEyes`, que consultava
   `vr_wpn_model` (ainda `"-"`), nunca desenhava. Hoje existe **uma decisão só**
   (`vr_hands_draw_ours`), tomada antes de qualquer `return`.
6. **A coluna 0 de uma matriz column-major é `(m[0] m[1] m[2])`.** Usar a linha 0
   (`m[0] m[4] m[8]`) fazia o diagnóstico acusar **86° de erro** num encaixe que estava
   certo — custou uma folha de contato inteira de investigação.
7. **O resíduo gira no LOCAL do referencial em que a arma é montada.** Antes ele usava o
   vetor de mundo do referencial: com o corpo virado para +X dava certo e fora disso o
   coldre de −70° saía com **≈16°** (a pistola praticamente deitada na barriga).
8. **A média dos dedos divide por QUANTOS dedos foram somados.** Dividir por 3 com 4 dedos
   somados inflava a "frente da palma" em 4/3 e jogava o encaixe para fora do lugar.
9. **O `test_nodepth` do diagnóstico vazava para o jogo** (sem profundidade a arma
   atravessava parede em VR); hoje o modo de teste é explícito (`vr_hands_set_test_mode` foi
   substituído por um modo de folha explícito).
10. **No modo de teste o `update` não rodava**: ele estava só dentro do bloco de sessão do
    `VR_FramePre`, que não existe sem headset. A folha de contato ficava em branco.
