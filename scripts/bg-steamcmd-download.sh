#!/bin/bash
# Baixa o conteúdo base do HLDS + cstrike via SteamCMD (app 90, login anônimo).
# O app 90 é conhecido por precisar de mais de uma passada para baixar tudo.
set -u
CS=/home/willianvalle/Workspace/cs-opensource
mkdir -p "$CS/server" "$CS/logs"

ok=0
for i in 1 2 3 4; do
  echo "=== steamcmd: passada $i ==="
  podman run --rm -v "$CS/server":/data:Z docker.io/steamcmd/steamcmd:latest \
    +force_install_dir /data \
    +login anonymous \
    +app_set_config 90 mod cstrike \
    +app_update 90 validate \
    +quit && ok=1 && break
  echo "=== passada $i falhou, tentando de novo ==="
done

echo "=== resultado: ok=$ok ==="
ls -la "$CS/server" 2>&1
[ -f "$CS/server/hlds_linux" ] && echo "HLDS_OK" || echo "HLDS_MISSING"
[ -d "$CS/server/cstrike" ] && echo "CSTRIKE_OK" || echo "CSTRIKE_MISSING"
