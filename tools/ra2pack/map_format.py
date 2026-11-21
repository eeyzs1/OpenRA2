"""Strict readers for the original TS/RA2 map packs (no asset discovery).

Format references: EA's CNC_TS_and_RA2_Mission_Editor, MapData.cpp,
MissionEditorPackLib.cpp and XCC cc_structures.h; OpenRA's ImportGen2MapCommand.
Both overlay packs use length-prefixed LCW blocks, not a bare LCW stream.
"""
import base64
from dataclasses import dataclass
import struct


def read_ini(data):
    # The shipped rules.ini contains un-commented prose and two malformed list
    # entries. Westwood ignores non-assignment lines; ConfigParser rejects them.
    sections, current = {}, None
    for line in data.decode("cp1252").splitlines():
        line = line.split(";", 1)[0].strip()
        if line.startswith("[") and "]" in line:
            current = sections.setdefault(line[1:line.index("]")].strip().lower(), {})
        elif current is not None and "=" in line:
            key, value = line.split("=", 1)
            if key.strip():
                current[key.strip().lower()] = value.strip()
    return sections


def decode_lcw(data, expected):
    """Decode Format80, checking every read, back-reference and output bound."""
    cursor = 0
    result = bytearray()

    def take(count):
        nonlocal cursor
        if count < 0 or cursor + count > len(data):
            raise ValueError("truncated LCW command")
        value = data[cursor:cursor + count]
        cursor += count
        return value

    def word():
        return int.from_bytes(take(2), "little")

    while True:
        code = take(1)[0]
        if code == 0x80:
            if len(result) != expected or cursor != len(data):
                raise ValueError("LCW length/trailing data mismatch")
            return bytes(result)
        if code < 0x80:
            count = (code >> 4) + 3
            source = len(result) - (((code & 15) << 8) | take(1)[0])
        elif code < 0xC0:
            count = code & 63
            if len(result) + count > expected:
                raise ValueError("LCW literal overrun")
            result.extend(take(count))
            continue
        elif code == 0xFE:
            count, value = word(), take(1)[0]
            if not count or len(result) + count > expected:
                raise ValueError("LCW fill overrun")
            result.extend(bytes([value]) * count)
            continue
        else:
            count = word() if code == 0xFF else (code & 63) + 3
            source = word()
        if not count or source < 0 or source >= len(result) or len(result) + count > expected:
            raise ValueError("invalid LCW back-reference")
        # Forward copying is intentional: repetitions may overlap their source.
        for i in range(count):
            result.append(result[source + i])


def decode_pack(section, codec, expected):
    try:
        keys = sorted(int(k) for k in section)
        if keys != list(range(1, len(keys) + 1)):
            raise ValueError("non-contiguous map pack keys")
        data = base64.b64decode("".join(section[str(k)] for k in keys), validate=True)
    except (KeyError, TypeError) as exc:
        raise ValueError("invalid map pack") from exc
    cursor, result = 0, bytearray()
    while cursor < len(data):
        if cursor + 4 > len(data):
            raise ValueError("truncated map pack header")
        packed, unpacked = struct.unpack_from("<HH", data, cursor)
        cursor += 4
        if packed == unpacked == 0 and cursor == len(data):
            break
        if not packed or not 0 < unpacked <= 8192 or cursor + packed > len(data):
            raise ValueError("invalid map pack block")
        if len(result) + unpacked > expected:
            raise ValueError("map pack output overrun")
        block = data[cursor:cursor + packed]
        cursor += packed
        if codec == "lzo":
            try:
                import lzokay
            except ImportError as exc:
                raise RuntimeError("Install tools/ra2pack/requirements.txt to import native maps") from exc
            decoded = lzokay.decompress(block, unpacked)
        elif codec == "lcw":
            decoded = decode_lcw(block, unpacked)
        else:
            raise ValueError("unknown map pack codec")
        if len(decoded) != unpacked:
            raise ValueError("map pack decoded length mismatch")
        result.extend(decoded)
    if len(result) != expected:
        raise ValueError(f"map pack length {len(result)} != {expected}")
    return bytes(result)


@dataclass(frozen=True)
class IsoTile:
    x: int
    y: int
    tile: int
    auxiliary: int
    subtile: int
    height: int
    auxiliary2: int


@dataclass
class NativeMap:
    sections: dict
    width: int
    height: int
    local: tuple
    theater: str
    tiles: list
    overlays: bytes
    overlay_data: bytes

    def projected(self, x, y):
        """Original staggered coordinates: two half-height rows per INI row."""
        return x - y + self.width - 1, x + y - self.width - 1


def parse_map(data):
    sections = read_ini(data)
    try:
        size = tuple(int(x.strip()) for x in sections["map"]["size"].split(","))
        local = tuple(int(x.strip()) for x in sections["map"]["localsize"].split(","))
        theater = sections["map"]["theater"].upper()
    except (KeyError, ValueError) as exc:
        raise ValueError("invalid native map header") from exc
    if len(size) != 4 or len(local) != 4 or size[:2] != (0, 0):
        raise ValueError("unsupported native map origin")
    w, h = size[2:]
    if not 1 <= w <= 256 or not 1 <= h <= 256 or w + h > 512:
        raise ValueError("native map exceeds engine canvas")
    if min(local) < 0 or not local[2] or not local[3] or local[0] + local[2] > w or local[1] + local[3] > h:
        raise ValueError("invalid native map local bounds")
    count = (2 * w - 1) * h
    raw = decode_pack(sections["isomappack5"], "lzo", count * 11 + 4)
    if raw[-4:] != bytes(4):
        raise ValueError("invalid IsoMapPack5 sentinel")
    tiles = [IsoTile(*v) for v in struct.iter_unpack("<HHHHBBB", raw[:-4])]
    expected = {((dx + 2 * y + dx % 2) // 2 + 1,
                 2 * y + dx % 2 - (dx + 2 * y + dx % 2) // 2 + w)
                for y in range(h) for dx in range(2 * w - 1)}
    if len(tiles) != len(expected) or {(t.x, t.y) for t in tiles} != expected:
        raise ValueError("duplicate, missing or out-of-canvas native tiles")
    if any(t.height > 14 for t in tiles):
        raise ValueError("invalid native terrain height")
    overlays = decode_pack(sections["overlaypack"], "lcw", 512 * 512)
    overlay_data = decode_pack(sections["overlaydatapack"], "lcw", 512 * 512)
    return NativeMap(sections, w, h, local, theater, tiles, overlays, overlay_data)
