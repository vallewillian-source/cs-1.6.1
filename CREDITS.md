# Créditos e licenças

Este projeto usa tanto código próprio quanto conteúdo de terceiros. Este arquivo
lista o que vem de fora e sob qual licença, para que a origem fique rastreável.

## Assets 3D do viewmodel VR (braços + arma)

Ambos são **CC0 1.0 Universal** (domínio público): uso livre, inclusive comercial,
**sem exigência de atribuição**. O crédito aqui é cortesia.

| Asset | Autor | Licença | Fonte |
|---|---|---|---|
| **WRAD ARMS** — braços de viewmodel em 1ª pessoa, malha com skin, 1.196 tri | *wriks* | CC0 1.0 | <https://github.com/wwwriks/wrad-arms> (também em <https://wriks.itch.io/wrad-arms>) |
| **Ultimate Guns Pack** — `Pistol_1.obj` (580 vértices, 5 materiais) | **Quaternius** | CC0 | <https://opengameart.org/content/low-poly-guns-pack> (upload do próprio autor; o mesmo pack existe em <https://quaternius.com> e <https://poly.pizza>) |

Detalhes de aquisição, escala e o que o binário de cada asset dizem estão em
[`mods/vr/assets/SOURCES.md`](mods/vr/assets/SOURCES.md).

## Bibliotecas de terceiros embutidas no mod VR

| Biblioteca | Autor | Licença | Uso |
|---|---|---|---|
| **cgltf** | Johannes Kuhlmann | MIT | leitura de glTF/GLB (malha, skin, textura embutida) |
| **stb_image** | Sean Barrett (nothings) | domínio público / MIT | decodificação da textura PNG |

Ambas são header-only e estão em `mods/vr/third_party/`.

## Código do motor e do jogo

- **Xash3D-FWGS** (engine) — GPL-3.0
- **ReGameDLL_CS** e **cs16-client** — ver os LICENSE dos respectivos projetos
- **OpenXR-SDK** — Apache-2.0
