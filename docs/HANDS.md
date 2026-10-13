# Viewmodel próprio (braços + arma) — `vr_hands`

Substitui o viewmodel de 1999 do CS, **na classe `pistol` e só em VR**, por braços
com skin (WRAD ARMS) e uma pistola (Quaternius), os dois seguindo as mãos.

## O problema que isto resolve

Até a Fase 6 do VR, a arma na mão era o **viewmodel do próprio jogo** deslocado por
um offset (`vr_weapon.cfg`). Isso funciona, mas traz junto os braços de 1999 soldados
à arma — e eles não acompanham as mãos. Aqui o jogo para de desenhar o viewmodel
dele (`RF_NO_VIEWMODEL`) e o mod desenha o seu próprio.

## O que a ABI permite (e por que o subsistema é assim)

A ABI do Mod (v8) expõe ao Mod **apenas** `Printf`, `DoubleTime` e `engine_version`
(`engine/common/mod_api.h:69-75`): não há carregador de modelo/textura nem helper de
desenho. Em compensação:

- o **FBO do olho já tem profundidade** (`GL_DEPTH_COMPONENT24`, `mod_vr.cpp`), então
  desenhar com `GL_DEPTH_TEST` faz o mundo **ocluir** os braços de graça;
- o **`mod_viewmodel_t.hide`** existe e era o gancho reservado para exatamente isto;
- `vr_eye_cam[e]` guarda origem/ângulos/`fov_asym` de cada olho — é dele que sai a
  projeção.

Por isso o subsistema **carrega as próprias funções de GL** (`dlsym` em `libGL`) em vez
de entrar no carregador do mod, que serve ao painel 2D e falha em bloco: aqui, faltar
uma função só desliga este subsistema.

## Arquivos

| Arquivo | O que é |
|---|---|
| `mods/vr/vr_hands.h` | interface (não depende do engine nem da ABI) |
| `mods/vr/vr_hands.cpp` | carga glTF/OBJ, skinning, IK, desenho |
| `mods/vr/vr_hands.cfg` | calibração (semente em `game/mods/`) |
| `mods/vr/assets/` | assets CC0 + `SOURCES.md` |
| `mods/vr/third_party/` | cgltf (MIT) + stb_image (domínio público) |
| `mods/vr/tools/glb_inspect.py` | inspetor de GLB (ossos, skin, bbox, pose) |
| `mods/vr/tools/skin_probe.cpp` | roda o skinning na CPU sem GL (diagnóstico) |

## Fluxo por frame

```
VR_Init        -> vr_hands_init()   : carrega assets + GL (falha não derruba o mod)
VR_ViewModel   -> vr_hands_covers_model(nome) ? vm->hide = 1 : (comportamento antigo)
VR_AfterEyes   -> por olho: liga o FBO daquele olho e chama vr_hands_draw(cam, mãos)
```

Dentro de `vr_hands_draw`: coloca o rig (girado pelo yaw da cabeça, com o osso `head`
ancorado na posição do HMD) → resolve **IK de dois ossos** por braço até a pose da mão
correspondente → calcula o skinning **uma vez por frame** (os dois olhos só trocam a
matriz) → desenha. A arma vai no osso **`socket.r`**, que o próprio rig já traz.

## Configuração (`game/mods/vr_hands.cfg`)

Relido por mtime (~1×/s): **salvar já aplica, sem reiniciar.**

| Chave | Para quê |
|---|---|
| `enable` | 0 desliga tudo (volta ao viewmodel do jogo) |
| `arms_glb`, `arms_tex`, `weapon_obj` | caminhos dos assets |
| `scale_arms`, `scale_weapon` | **a calibração principal** (ver abaixo) |
| `weapon_euler`, `weapon_off` | orientação/posição da arma dentro do soco |
| `rig_yaw` | giro fino do conjunto (ombros) |
| `near`, `far` | profundidade — precisam casar com o ref, senão a oclusão erra |
| `color_gamma` | o `Kd` do MTL é linear e escuro; isto o traz para sRGB |

**Escala:** os assets vêm no espaço do autor, não em metros. Medido no rig: o alcance
do braço (bíceps→punho) é 5,20 unidades do modelo, e um braço real (~55 cm) dá
≈22 unidades GoldSrc → **≈4,2**. A pistola mede 1,82 unidades → 7,6 unidades = 19 cm,
que confere. Os dois batem, então 4,2 é o ponto de partida.

## Diagnóstico sem headset

Com a variável de ambiente `VR_HANDS_TEST=1`, o mod desenha o viewmodel próprio **na
janela** usando a câmera do engine, mesmo sem sessão OpenXR. Some com `VR_PANEL_DUMP`
para tirar uma imagem:

```sh
cd game
VR_HANDS_TEST=1 VR_DEBUG_PANEL=0 VR_PANEL_DUMP=/tmp/hands.ppm \
  SDL_VIDEODRIVER=x11 ./xash3d -game cstrike -mod vr +map de_dust2
```

Chaves de diagnóstico no cfg: `synth_hands` (ignora os controles e usa duas mãos de
mentira — é o que prova a IK sem headset), `test_dist/…`, `test_nodepth`, `test_flat`.

O painel de debug do mod mostra uma linha `maos: ON br=… wi=… es=…/…`.

## O que já foi verificado (sem headset)

| Prova | Como |
|---|---|
| glTF carrega com skin e textura | log: `1196 tri, 872 verts, 50 ossos, skin=sim, tex=62` |
| OBJ carrega com materiais | log: `1040 tri, 580 verts, 5 materiais` |
| o skinning não colapsa | `tools/skin_probe.cpp`: bbox skin == bbox repouso |
| a projeção casa com o mundo | dump com o modelo a 40 unidades, centrado |
| a IK dobra os braços até as mãos | dump com `synth_hands`: braços + pistola na mão |
| o modo flat não regride | sem `VR_HANDS_TEST`, nada é escondido nem desenhado |

**Falta calibrar com o headset:** escala fina, orientação da pistola no soco
(`weapon_euler`/`weapon_off`), e a posição/altura do conjunto (`rig_yaw`).

## Limites desta V1

- Só a classe **pistol** (usp, glock18, p228, deagle, elite, fiveseven). As outras
  classes continuam com o viewmodel do jogo.
- Sem animação de verdade: a pose é conduzida pelas mãos + um **recuo procedural**
  (kick + giro) disparado na borda do gatilho. Não há recarga, saque ou holster.
- Os dedos ficam na curvatura de repouso (não pegam no carregador).
- Skinning na **CPU** em modo imediato. Para ~1.200 tri é irrelevante; se um dia
  pesar, o caminho é arrays de vértice, sem mudar o resto.

## Notas de engenharia (bugs que custaram e por quê)

1. **`fov_asym` é em RADIANOS.** A ABI entrega ângulos e o `proj_matrix` aplica `tan()`
   neles. Guardar a *tangente* na síntese do modo flat aplicava o `tan` duas vezes e
   achatava o modelo em filetes verticais.
2. **A inversa tem de ser geral, não "rígida".** As matrizes de mundo do rig carregam a
   escala (`scale_arms`); usar uma inversa que assume rotação ortonormal gerava locais
   com translação de centenas de unidades e a malha explodia.
3. **O skin já está em espaço de mundo.** O `cur[root]` inclui a colocação do rig, então
   o desenho dos braços entra com a matriz **identidade** — aplicar a colocação de novo
   jogava os braços para longe (a arma, que não tem skin, continuava certa).
4. **Modo imediato com *client arrays* ligados é descartado.** O `ref_gl` desenha o mundo
   com vertex arrays; desligar tudo antes do `glBegin` evita vértices descartados.
