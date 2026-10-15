#!/usr/bin/env python3
"""Exercise the real native session through local IPC and X11 keyboard events.

Run with --window <X11 id> against an already-running managed test session.
Does not use a model stub. Saves measurements in ignored build/validation/.
"""
from __future__ import annotations
import argparse
import ctypes as C
import json
from pathlib import Path
import statistics
import struct
import time

ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / "game/mod-state"
REPORT = []


def read(name):
    return json.loads((STATE / "host" / name).read_text())


def wait(predicate, label, timeout=8):
    start = time.monotonic()
    while time.monotonic()-start < timeout:
        try:
            if predicate():
                elapsed = time.monotonic()-start
                REPORT.append(dict(check=label, seconds=round(elapsed, 3)))
                print(f"PASS {label} ({elapsed:.3f}s)", flush=True)
                return
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(.05)
    raise AssertionError(label)


def request(ident, enabled):
    control=read("control.json")
    path=STATE / "host/requests" / f"{time.monotonic_ns()}.json"
    tmp=path.with_suffix(".tmp")
    tmp.write_text(json.dumps(dict(session=control["session"],seq=time.monotonic_ns(),id=ident,enabled=enabled)))
    tmp.replace(path)


def active(ident):
    return read("status.json")["mods"][ident]["active"]


class Key(C.Structure):
    _fields_=[("type",C.c_int),("serial",C.c_ulong),("send_event",C.c_int),("display",C.c_void_p),
              ("window",C.c_ulong),("root",C.c_ulong),("subwindow",C.c_ulong),("time",C.c_ulong),
              ("x",C.c_int),("y",C.c_int),("x_root",C.c_int),("y_root",C.c_int),
              ("state",C.c_uint),("keycode",C.c_uint),("same_screen",C.c_int)]
class Event(C.Union):
    _fields_=[("xkey",Key),("pad",C.c_long*24)]


class Keyboard:
    def __init__(self, window):
        self.window=window;self.x=C.CDLL("libX11.so.6")
        self.x.XOpenDisplay.restype=C.c_void_p
        self.x.XKeysymToKeycode.argtypes=[C.c_void_p,C.c_ulong];self.x.XKeysymToKeycode.restype=C.c_uint
        self.x.XDefaultRootWindow.argtypes=[C.c_void_p];self.x.XDefaultRootWindow.restype=C.c_ulong
        self.x.XSendEvent.argtypes=[C.c_void_p,C.c_ulong,C.c_int,C.c_long,C.POINTER(Event)]
        self.x.XSync.argtypes=[C.c_void_p,C.c_int]
        self.display=self.x.XOpenDisplay(None)
        if not self.display:raise RuntimeError("X11 display unavailable")

    def press(self, keysym):
        for kind,mask in ((2,1),(3,2)):
            e=Event();e.xkey=Key(kind,0,1,self.display,self.window,self.x.XDefaultRootWindow(self.display),0,
                                0,10,10,10,10,0,self.x.XKeysymToKeycode(self.display,keysym),1)
            self.x.XSendEvent(self.display,self.window,True,mask,C.byref(e));self.x.XSync(self.display,False)
            time.sleep(.06)


def players():
    body=(STATE/"x-ray-debug/channels/players.bin").read_bytes()[88:]
    count=struct.unpack_from("<i",body,180)[0]
    return [struct.unpack_from("<Iiiiii64s9f",body,184+124*i) for i in range(count)]


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--window",type=lambda s:int(s,0),required=True)
    args=parser.parse_args();keyboard=Keyboard(args.window)
    wait(lambda:active("bots-jev") and active("x-ray-debug") and not active("vr"),"flat profile confirmed")
    wait(lambda:sum(p[4] for p in players())==8,"eight bots in authoritative snapshot")
    if read("client.json")["ui"]["capture"]:keyboard.press(0xffc3)
    keyboard.press(0xffc3)
    wait(lambda:read("client.json")["ui"]["capture"]==1,"F6 opens manager")
    old=read("client.json")["ui"]["panels"];keyboard.press(0xffc4)
    wait(lambda:read("client.json")["ui"]["panels"]!=old,"F7 hides selected panels")
    assert active("bots-jev") and active("x-ray-debug")
    keyboard.press(0xffc4)
    wait(lambda:read("client.json")["ui"]["panels"]==old,"F7 restores panel set without toggling mods")
    # The selected package is read from telemetry, not assumed to be a fixed index.
    ordered=sorted(read("client.json")["mods"])
    selected=read("client.json")["ui"]["selected"]
    while selected!=ordered.index("bots-jev"):
        keyboard.press(0xff54);selected=(selected+1)%len(ordered)
    keyboard.press(0xffc5)
    wait(lambda:not active("bots-jev") and not read("status.json")["mods"]["bots-jev"]["components"]["sidecar"],"F8 disables Jev and its sidecar")
    assert sum(p[4] for p in players())==8
    before={p[0]:p[7:10] for p in players() if p[4] and p[3]}
    wait(lambda:any(p[0] in before and p[7:10]!=before[p[0]] for p in players() if p[4] and p[3]),"stock bots keep moving while Jev is disabled")
    keyboard.press(0xffc5)
    wait(lambda:active("bots-jev"),"F8 restores Jev components")
    for ident in ("x-ray-debug","vr","null"):
        request(ident,True);wait(lambda ident=ident:active(ident),f"hot enable {ident}")
        request(ident,False);wait(lambda ident=ident:not active(ident),f"hot disable {ident}")
    request("x-ray-debug",True);wait(lambda:active("x-ray-debug"),"x-ray restored")
    for ident in ("test-bad-abi","test-bad-size","test-no-symbol","test-refuse-init","test-missing"):
        if ident not in ordered:continue
        request(ident,True)
        wait(lambda ident=ident:bool(read("client.json")["mods"][ident]["error"]),f"loader rejects {ident} without stopping game")
        assert active("bots-jev") and active("x-ray-debug")
        request(ident,False)
    if "test-camera" in ordered:
        request("test-camera",True);wait(lambda:active("test-camera"),"SDK-only camera fixture active")
        request("vr",True);wait(lambda:"Conflito" in read("client.json")["mods"]["vr"]["error"],"exclusive camera conflict reported")
        request("test-camera",False);wait(lambda:active("vr"),"pending VR activates after camera owner disables")
        request("vr",False);wait(lambda:not active("vr"),"VR disabled after conflict test")
    keyboard.press(0xffc3);wait(lambda:read("client.json")["ui"]["capture"]==0,"F6 returns input to game")
    samples=[]
    for _ in range(30):samples.append(read("client.json")["ui"]["frame_ms"]);time.sleep(.05)
    REPORT.append(dict(check="visible UI frame interval",median_ms=statistics.median(samples),max_ms=max(samples)))
    keyboard.press(0xffc4);time.sleep(.2)
    samples=[]
    for _ in range(30):samples.append(read("client.json")["ui"]["frame_ms"]);time.sleep(.05)
    REPORT.append(dict(check="hidden UI frame interval",median_ms=statistics.median(samples),max_ms=max(samples)))
    keyboard.press(0xffc4)
    output=ROOT/"build/validation/session-smoke.json";output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(REPORT,indent=2)+"\n")


if __name__=="__main__":main()
