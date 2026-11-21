"""Validate and describe native RIFF PCM/IMA files without changing samples."""
import hashlib
import struct


def inspect_wave(data):
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("missing RIFF/WAVE header (possibly a zero-filled placeholder)")
    if struct.unpack_from("<I", data, 4)[0] + 8 != len(data):
        raise ValueError("invalid RIFF length")
    pos, fmt, frames, payload = 12, None, None, None
    while pos + 8 <= len(data):
        kind, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        start = pos + 8
        if start + size > len(data):
            raise ValueError("truncated RIFF chunk")
        if kind == b"fmt " and size >= 16:
            fmt = struct.unpack_from("<HHIIHH", data, start)
        elif kind == b"fact" and size >= 4:
            frames = struct.unpack_from("<I", data, start)[0]
        elif kind == b"data":
            payload = data[start:start + size]
        pos = start + size + (size & 1)
    if not fmt or not payload:
        raise ValueError("missing wave format or sample data")
    codec, channels, rate, average, align, bits = fmt
    if codec not in (1, 17) or channels not in (1, 2) or not 4000 <= rate <= 96000 or not align:
        raise ValueError(f"unsupported native wave format {fmt}")
    if codec == 1:
        if bits != 16 or len(payload) % align:
            raise ValueError("invalid PCM16 data")
        frames = len(payload) // align
    if not frames:
        raise ValueError("missing or empty native sample count")
    return {"codec": codec, "channels": channels, "rate": rate, "frames": frames,
            "seconds": frames / rate, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def find_wave(tree, name, preferred=None):
    candidates = tree.find_all(name)
    if preferred:
        candidates.sort(key=lambda item: preferred not in item[0])
    errors = []
    for source, data in candidates:
        try:
            return source, data, inspect_wave(data)
        except ValueError as error:
            errors.append({"source": source, "reason": str(error)})
    return None, None, {"unavailable": errors or [{"reason": "not found"}]}
