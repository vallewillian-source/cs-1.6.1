"""Local modhost snapshot envelope, shared across 32/64-bit processes."""
from __future__ import annotations
import json
import os
import struct
import time
from pathlib import Path

HEADER = struct.Struct("<IIIId64s")
MAGIC = 0x43534D31


def read(path: Path, session: str, generation: int, max_age: float = 10) -> dict | None:
    try:
        if path.stat().st_size > 65536 + HEADER.size:
            return None
        raw = path.read_bytes()
        magic, size, gen, _, timestamp, identity = HEADER.unpack_from(raw)
        age = time.monotonic() - timestamp
        if (magic != MAGIC or size != len(raw) - HEADER.size or gen != generation
                or identity.rstrip(b"\0").decode() != session or not 0 <= age <= max_age):
            return None
        result = json.loads(raw[HEADER.size:])
        return result if isinstance(result, dict) else None
    except (OSError, ValueError, struct.error, UnicodeError):
        return None


def write(path: Path, value: dict, session: str, generation: int) -> None:
    body = json.dumps(value, allow_nan=False, separators=(",", ":")).encode()
    if len(body) > 65536:
        raise ValueError("snapshot exceeds 64 KiB")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".{os.getpid()}.tmp")
    tmp.write_bytes(HEADER.pack(MAGIC, len(body), generation, 0, time.monotonic(), session.encode()) + body)
    tmp.replace(path)
