#!/usr/bin/env python3
"""One local session owns its client, dedicated server and package sidecars."""
from __future__ import annotations
import argparse
import fcntl
import json
import os
from pathlib import Path
import queue
import re
import signal
import socket
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,62}\Z")


def read_json(path: Path) -> dict:
    try:
        if path.stat().st_size > 1024 * 1024:
            return {}
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def atomic_json(path: Path, value: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".{os.getpid()}.tmp")
    tmp.write_text(json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n")
    tmp.replace(path)


def packages(root: Path) -> dict:
    found = {}
    for folder in sorted(root.iterdir()):
        if not folder.is_dir() or not ID.fullmatch(folder.name) or not (folder / "mod.json").exists():
            continue
        manifest = read_json(folder / "mod.json")
        error = None
        try:
            if manifest.get("manifest_version") != 1 or manifest.get("id") != folder.name:
                raise ValueError("Manifesto inválido ou id diferente da pasta")
            components = manifest.get("components", {})
            if not isinstance(components, dict):
                raise ValueError("components deve ser um objeto")
            for kind in ("client", "server"):
                if kind in components:
                    value = components[kind]
                    if not isinstance(value, str) or not (folder / value).resolve().is_relative_to(folder.resolve()):
                        raise ValueError(f"Componente {kind} fora do pacote")
            sidecar = components.get("sidecar")
            if sidecar is not None:
                if not isinstance(sidecar, list) or len(sidecar) < 2 or sidecar[0] != "python" or not all(isinstance(x, str) for x in sidecar):
                    raise ValueError("Sidecar deve ser [python, caminho, argumentos…]")
                if not (folder / sidecar[1]).resolve().is_relative_to(folder.resolve()):
                    raise ValueError("Sidecar fora do pacote")
        except (ValueError, TypeError) as exc:
            error = str(exc)
            manifest = dict(id=folder.name, components={}, defaults={"enabled": False})
        manifest["folder"] = folder
        manifest["error"] = error
        found[folder.name] = manifest
    return found


def stop_process(process: subprocess.Popen, grace: float = 5):
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=2)


class Session:
    def __init__(self, args):
        self.args = args
        self.root = ROOT / "game/mods"
        self.state = ROOT / "game/mod-state"
        self.state.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.state.chmod(0o700)
        self.lease = (self.state / "session.lock").open("w")
        try:
            fcntl.flock(self.lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("Já existe uma sessão gerenciada neste diretório") from None
        self.mods = packages(self.root)
        self.profile = read_json(ROOT / "profiles" / (args.profile + ".json"))
        if not self.profile:
            raise ValueError("Perfil ausente ou inválido")
        self.session = uuid.uuid4().hex
        self.control = {"session": self.session, "mods": {}}
        previous = read_json(self.state / "host/desired.json") if args.restore else {}
        for ident, mod in self.mods.items():
            enabled = previous.get(ident, self.profile.get("mods", {}).get(ident, mod.get("defaults", {}).get("enabled", False)))
            self.control["mods"][ident] = dict(enabled=bool(enabled) and not mod.get("error"), generation=1)
        self.sidecars = {}
        self.sidecar_errors = {}
        self.server = self.client = None
        self.container = "cs161-" + self.session[:12]
        self.port = args.port or self.profile.get("port", 27015)
        self.log_queue = queue.Queue(maxsize=4096)
        self.log_threads = []
        self.handles = []
        self.env = dict(os.environ, CS_MOD_MANAGED="1", CS_MOD_SESSION=self.session,
                        CS_MOD_ROOT=str(self.root), CS_MOD_STATE_ROOT=str(self.state),
                        CS_MOD_SERVER=f"127.0.0.1:{self.port}")
        self.flush_control()
        atomic_json(self.state / "host/session.json", {"session": self.session, "pid": os.getpid(), "container": self.container})

    def flush_control(self):
        atomic_json(self.state / "host/control.json", self.control)
        atomic_json(self.state / "host/desired.json", {k: v["enabled"] for k, v in self.control["mods"].items()})

    def append_log(self, ident: str, message: str, severity=0):
        # Packages may emit structured severities; plain process output remains valid.
        try:
            record = json.loads(message)
            if isinstance(record, dict) and isinstance(record.get("message"), str):
                message = record["message"]
                if type(record.get("severity")) is int and 0 <= record["severity"] <= 2:
                    severity = record["severity"]
        except ValueError:
            pass
        path = self.state / ident / "logs/sidecar.jsonl"
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists() and path.stat().st_size >= 4 * 1024 * 1024:
            old = path.with_suffix(".jsonl.1")
            if old.exists():
                old.replace(path.with_suffix(".jsonl.2"))
            path.replace(old)
        with path.open("a") as f:
            f.write(json.dumps(dict(time=time.monotonic(), session=self.session, origin="sidecar", severity=severity, message=message[:1024]), ensure_ascii=False) + "\n")

    def collect_log(self, ident, stream):
        try:
            while True:
                line = stream.readline(4097)
                if not line:
                    break
                try:
                    self.log_queue.put_nowait((ident, line.rstrip()[:4096]))
                except queue.Full:
                    pass
        finally:
            stream.close()

    def start_server(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", self.port))  # Refuse occupied ports; never replace another server.
        log_dir = ROOT / "logs"
        log_dir.mkdir(exist_ok=True)
        output = (log_dir / "managed-server.log").open("w")
        self.handles.append(output)
        command = ["podman", "run", "--rm", "-i", "--name", self.container, "--network", "host",
                   "--security-opt", "label=disable", "-v", f"{ROOT / 'server'}:/server",
                   "-v", f"{self.root}:/cs-mods:ro", "-v", f"{self.state}:/cs-mod-state",
                   "-e", "CS_MOD_MANAGED=1", "-e", f"CS_MOD_SESSION={self.session}",
                   "-e", "CS_MOD_ROOT=/cs-mods", "-e", "CS_MOD_STATE_ROOT=/cs-mod-state",
                   "-e", "LD_LIBRARY_PATH=/usr/lib32:/usr/lib/i386-linux-gnu:/lib/i386-linux-gnu:/server", "-w", "/server", "--entrypoint", "/server/hlds_linux",
                   "docker.io/steamcmd/steamcmd:latest", "-game", "cstrike", "-console", "-port", str(self.port),
                   "+map", self.args.map or self.profile.get("map", "de_dust2"), "+maxplayers", "12",
                   "+bot_quota", str(self.profile.get("bots", 8)), "+bot_quota_mode", "normal",
                   "+bot_join_after_player", "0", "+bot_difficulty", "2", "+mp_autoteambalance", "0",
                   "+mp_limitteams", "0", "-pingboost", "2", "+sys_ticrate", "1000", "-nobreakpad"]
        self.server = subprocess.Popen(command, stdin=subprocess.PIPE, text=True, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if self.server.poll() is not None:
                raise RuntimeError("Servidor encerrou; consulte logs/managed-server.log")
            status = read_json(self.state / "host/server.json")
            if status.get("session") == self.session and time.monotonic() - status.get("time", 0) < 2:
                # Verify the game protocol too, not just an open UDP socket.
                if self.server_ready():
                    commands = f"bot_join_after_player 0\nbot_quota_mode normal\nbot_difficulty 2\nmp_autoteambalance 0\nmp_limitteams 0\nbot_quota {self.profile.get('bots', 8)}\nstatus\n"
                    self.server.stdin.write(commands)
                    self.server.stdin.flush()
                    return
            self.tick()
            time.sleep(.1)
        raise TimeoutError("Servidor não ficou pronto em 90 segundos")

    def server_ready(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(.15)
                sock.sendto(b"\xff\xff\xff\xffTSource Engine Query\0", ("127.0.0.1", self.port))
                data, _ = sock.recvfrom(4096)
                if data[:5] == b"\xff\xff\xff\xffA" and len(data) >= 9:
                    sock.sendto(b"\xff\xff\xff\xffTSource Engine Query\0" + data[5:9], ("127.0.0.1", self.port))
                    data, _ = sock.recvfrom(4096)
                return data[:5] in (b"\xff\xff\xff\xffI", b"\xff\xff\xff\xffm")
        except OSError:
            return False

    def tick(self):
        requests = self.state / "host/requests"
        requests.mkdir(parents=True, exist_ok=True)
        for path in sorted(requests.glob("*.json"), key=lambda p: int(p.stem) if p.stem.isdigit() else 0):
            req = read_json(path)
            path.unlink(missing_ok=True)
            ident = req.get("id")
            if req.get("session") != self.session or ident not in self.mods or self.mods[ident].get("error") or type(req.get("enabled")) is not bool:
                continue
            target = self.control["mods"][ident]
            if target["enabled"] != req["enabled"]:
                target["enabled"] = req["enabled"]
                target["generation"] += 1
                self.sidecar_errors.pop(ident, None)
                self.flush_control()
        native = {kind: read_json(self.state / f"host/{kind}.json") for kind in ("client", "server")}
        status = {"session": self.session, "time": time.monotonic(), "mods": {}}
        for ident, mod in self.mods.items():
            target = self.control["mods"][ident]
            components = mod["components"]
            errors = [mod["error"]] if mod.get("error") else []
            states = {}
            for kind in ("client", "server"):
                if kind not in components:
                    continue
                snapshot = native[kind]
                alive = snapshot.get("session") == self.session and 0 <= time.monotonic() - snapshot.get("time", 0) < 2
                component = snapshot.get("mods", {}).get(ident, {}) if alive else {}
                states[kind] = bool(component.get("enabled")) and component.get("generation") == target["generation"]
                if component.get("error"):
                    errors.append(kind + ": " + component["error"])
            if "sidecar" in components:
                process = self.sidecars.get(ident)
                if process is not None and process.poll() is not None:
                    self.sidecar_errors[ident] = f"Sidecar encerrou (código {process.returncode}); desative e reative para tentar novamente"
                if not target["enabled"] and process is not None:
                    stop_process(process)
                    del self.sidecars[ident]
                    self.sidecar_errors.pop(ident, None)
                elif target["enabled"] and process is None and ident not in self.sidecar_errors and states.get("server", True):
                    python = self.state / ident / ".venv/bin/python"
                    if not python.is_file():
                        self.sidecar_errors[ident] = "Ambiente Python ausente; execute scripts/setup-mods.sh"
                    else:
                        command = [str(python), "-u", str(mod["folder"] / components["sidecar"][1]), *components["sidecar"][2:]]
                        env = dict(self.env, CS_MOD_STATE=str(self.state / ident), CS_MOD_GENERATION=str(target["generation"]))
                        process = subprocess.Popen(command, env=env, cwd=mod["folder"], stdout=subprocess.PIPE,
                                                   stderr=subprocess.STDOUT, text=True, errors="replace", start_new_session=True)
                        self.sidecars[ident] = process
                        thread = threading.Thread(target=self.collect_log, args=(ident, process.stdout), daemon=True)
                        thread.start()
                        self.log_threads.append(thread)
                if ident in self.sidecar_errors:
                    errors.append(self.sidecar_errors[ident])
                states["sidecar"] = ident in self.sidecars and self.sidecars[ident].poll() is None
            status["mods"][ident] = dict(desired=target["enabled"], active=bool(states) and all(states.values()), components=states, errors=errors)
        for _ in range(256):
            try:
                ident, line = self.log_queue.get_nowait()
            except queue.Empty:
                break
            self.append_log(ident, line, 2 if "Traceback" in line or "ausente" in line else 0)
        atomic_json(self.state / "host/status.json", status)

    def run(self):
        print(f"CS 1.6.1 · sessão {self.session[:8]} · perfil {self.args.profile}", flush=True)
        self.start_server()
        if self.args.server_only:
            while self.server.poll() is None:
                self.tick()
                time.sleep(.1)
            return self.server.returncode
        client_log = (ROOT / "logs/managed-client.log").open("w")
        self.handles.append(client_log)
        env = dict(self.env, SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "x11"))
        # Use the existing credentials/config without copying secrets into the repo.
        command = [str(ROOT / "game/xash3d"), "-game", "cstrike", "-log", "managed-engine.log",
                   "+gl_vsync", "0", "+fps_max", "144", "+r_scene_scale", "1", "+gl_msaa", "0",
                   "+name", "Developer", "+connect", f"127.0.0.1:{self.port}", *self.args.client_args]
        self.client = subprocess.Popen(command, env=env, cwd=ROOT / "game", stdout=client_log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        atomic_json(self.state / "host/session.json", {"session": self.session, "pid": os.getpid(),
                    "client_pid": self.client.pid, "container": self.container})
        while self.client.poll() is None:
            if self.server.poll() is not None:
                raise RuntimeError("Servidor local encerrou durante a sessão")
            self.tick()
            time.sleep(.1)
        return self.client.returncode

    def close(self):
        if self.client is not None:
            stop_process(self.client)
        for process in self.sidecars.values():
            stop_process(process)
        if self.server is not None:
            # Only this unique session container is eligible for cleanup.
            subprocess.run(["podman", "stop", "--time", "3", self.container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
            stop_process(self.server)
        for thread in self.log_threads:
            thread.join(timeout=.2)
        while True:
            try:
                ident, line = self.log_queue.get_nowait()
            except queue.Empty:
                break
            self.append_log(ident, line)
        for handle in self.handles:
            handle.close()
        atomic_json(self.state / "host/status.json", {"session": self.session, "time": time.monotonic(), "closed": True})
        self.lease.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("flat-dev", "vr"), default="flat-dev")
    parser.add_argument("--restore", action="store_true", help="restaurar ativações salvas")
    parser.add_argument("--server-only", action="store_true", help="executar somente servidor e sidecars")
    parser.add_argument("--port", type=int)
    parser.add_argument("--map")
    parser.add_argument("client_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.port is not None and not 1024 <= args.port <= 65535:
        parser.error("porta deve estar entre 1024 e 65535")
    if args.map and not ID.fullmatch(args.map):
        parser.error("nome de mapa inválido")
    if args.client_args[:1] == ["--"]:
        args.client_args.pop(0)
    session = None
    def stop(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        session = Session(args)
        return session.run()
    except KeyboardInterrupt:
        return 0
    except (RuntimeError, OSError, ValueError) as exc:
        print(f"ERRO: {exc}", file=sys.stderr)
        return 1
    finally:
        if session is not None:
            session.close()


if __name__ == "__main__":
    raise SystemExit(main())
