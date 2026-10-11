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
