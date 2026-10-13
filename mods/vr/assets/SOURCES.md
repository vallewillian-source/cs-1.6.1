# Assets — proveniência, licenças e o que o binário diz

Tudo aqui é **CC0 1.0** (domínio público): uso livre, inclusive comercial, **sem exigência de
atribuição**. Mesmo assim os créditos vão em `CREDITS.md` na raiz, por cortesia e rastreio.

## Braços — `arms.glb` (WRAD ARMS, por *wriks*)

- Fonte: <https://github.com/wwwriks/wrad-arms> — arquivo bruto em
  `https://raw.githubusercontent.com/wwwriks/wrad-arms/main/arms.glb`
- Licença: CC0 1.0 (`LICENSE` nesta pasta; a API do GitHub confirma `spdx_id = CC0-1.0`)
- Arquivos usados: `arms.glb` (malha+esqueleto+textura embutida),
  `arm_albedo_pale.png` (512×512 RGBA) e `arm_albedo_dark.png` (2ª pele, opcional)
- **Relatório do rig** (gerado por `../tools/glb_inspect.py assets/arms.glb`):
  - 1 mesh: **872 vértices, 1196 triângulos**; atributos
    `POSITION/NORMAL/TEXCOORD_0/JOINTS_0/WEIGHTS_0` → **malha com skin** ✓
  - textura **PNG embutida no GLB** (`images[0] = arm_albedo_pale`, bufferView 6)
  - `skin[0]` com **50 ossos**: `root`, `shoulder.{r,l}`, `bicep`, `forearm`, `wrist`,
    `finger_{pinky,ring,middle,index,thumb}{1,2,3}`, **`socket.{r,l}`**,
    `forearm.Twist{0,1}`, **`wrist_ik.{r,l}`**, **`arm_target.{r,l}`**, `head`
  - **`socket.r` é o encaixe de arma do rig** → a pistola vai nele
  - **`wrist_ik` / `arm_target`** mostram que o rig foi montado para IK
  - Pose default **não é T-pose**: os braços já vêm numa pose natural de porte
  - bbox local ≈ **10,97 × 4,93 × 4,41** unidades → **escala não métrica** (calibrar por cfg)
  - **0 animações** (só o rig)
  - `head` em (0, +1,5, 0) e `root` em (0, −1, 0) → há um referencial de corpo no rig

## Pistola — `Pistol_1.obj` (Quaternius, *Ultimate Guns Pack*)

- Fonte: **OpenGameArt, upload do próprio Quaternius** —
  <https://opengameart.org/content/low-poly-guns-pack> →
  `ultimate_gun_pack_by_quaternius.zip` (7,4 MB)
- Licença: **CC0** (declarada na página do OGA: *"This pack contains 40 different gun
  models. All in FBX, OBJ and Blend formats, free to use in personal and commercial
  projects."*)
- O mesmo pack também existe em `quaternius.com` (Google Drive) e `poly.pizza`. No dia da
  aquisição o Drive respondeu **"quota exceeded"** e a API do poly.pizza exige chave — daí
  o OpenGameArt, que tem link direto e é o upload do autor.
- Arquivos usados: `Pistol_1.obj` + `Pistol_1.mtl` (o pack traz `Pistol_2..6` também)
- Geometria: **580 vértices, 487 faces**, **normais SIM, UVs NÃO** (`vt=0`)
- Cor: **5 materiais por `Kd`** (`Black`, `Black2`, `DarkMetal`, `DarkWood`, `Metal`) —
  **sem textura alguma** (o zip não traz nenhuma imagem além do `Preview.jpg`)
- Escala: ≈ **1,82 × 1,16 × 0,32** unidades — mesma ordem de grandeza do GLB dos braços
- Trocar de pistola = trocar o par `.obj/.mtl` (e o nome no cfg)

## Notas de calibração (para a Fase 2)

- Nenhum dos dois assets é métrico → a escala real vai no cfg, não no código.
- O `Kd` do MTL é **escuro e em linear** (ex.: `Metal` = 0,0533): converter para sRGB na
  hora de usar como cor (0,0533 → ≈ 0,26 de cinza), senão a arma sai quase preta.
- A pistola **não tem UV**, então ela é desenhada com **cor sólida por material**; os
  braços são texturizados. Para a arma não parecer chapada, aplicar sombreamento simples
  com a normal (`N·L` de uma luz fixa junto da câmera).
