# OpenRA2 asset pipeline — SHP(TS) pack, unittem quantize, MagicaVoxel→VXL/HVA writers.
# Does not depend on MIX; optional palette from ra2pack/game or assets.
from __future__ import annotations

import math
import os
import struct
import sys
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[2]
RA2PACK = ROOT / "tools" / "ra2pack"
sys.path.insert(0, str(RA2PACK))

try:
    from ra2_normals4 import NORMALS4
except Exception:
    NORMALS4 = [(0.0, 0.0, 1.0)] * 36

# Remap band in unittem.pal (house color placeholders)
REMAP_LO, REMAP_HI = 16, 31
SHADOW_IDX = 1
TRANSPARENT_IDX = 0


def load_unittem_pal(path: Optional[Path] = None) -> List[Tuple[int, int, int]]:
    """Load 256 RGB entries (0–255). Tries explicit path, then MIX extract via ra2lib, then assets."""
    candidates = []
    if path:
        candidates.append(Path(path))
    candidates += [
        ROOT / "assets" / "palettes" / "unittem.pal",
        RA2PACK / "game" / "unittem.pal",
    ]
    for p in candidates:
        if p.is_file():
            data = p.read_bytes()
            if len(data) >= 768:
                return [(data[i * 3] << 2, data[i * 3 + 1] << 2, data[i * 3 + 2] << 2) for i in range(256)]
    # MIX fallback
    try:
        from ra2lib import MixTree, load_pal
        T = MixTree()
        _, raw = T.find("unittem.pal")
        if raw:
            return load_pal(raw)
    except Exception:
        pass
    raise FileNotFoundError("unittem.pal not found (place under assets/palettes/ or tools/ra2pack/game/)")


def _dist2(a: Tuple[int, int, int], b: Tuple[int, int, int]) -> int:
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def nearest_index(rgb: Tuple[int, int, int], pal: Sequence[Tuple[int, int, int]],
                  forbid: Optional[set] = None) -> int:
    forbid = forbid or set()
    best, best_d = 2, 10 ** 9
    for i, c in enumerate(pal):
        if i in forbid:
            continue
        d = _dist2(rgb, c)
        if d < best_d:
            best, best_d = i, d
    return best


def is_remap_red(r: int, g: int, b: int, a: int) -> bool:
    return a > 40 and r >= 200 and g < 90 and b < 90


def is_shadowish(r: int, g: int, b: int, a: int) -> bool:
    # Translucent dark or near-black with low alpha
    if a < 8:
        return False
    if a < 160 and (r + g + b) < 90:
        return True
    return a > 40 and r + g + b < 24


def rgba_to_indexed(img, pal: Sequence[Tuple[int, int, int]], *,
                    force_remap_red: bool = True,
                    shadow_as_index1: bool = True) -> "Image.Image":
    """Convert RGBA PIL image to mode='P' with unittem indices. Index 0 = transparent."""
    from PIL import Image
    im = img.convert("RGBA")
    w, h = im.size
    px = im.load()
    out = Image.new("P", (w, h))
    # Build palette blob for PIL (768 bytes RGB)
    pal_flat = []
    for r, g, b in pal:
        pal_flat.extend([r, g, b])
    while len(pal_flat) < 768:
        pal_flat.append(0)
    out.putpalette(pal_flat[:768])
    forbid = {TRANSPARENT_IDX}
    if shadow_as_index1:
        forbid.add(SHADOW_IDX)
    # Prefer remap band when forcing house red
    data = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 16:
                data[y * w + x] = TRANSPARENT_IDX
                continue
            if shadow_as_index1 and is_shadowish(r, g, b, a):
                data[y * w + x] = SHADOW_IDX
                continue
            if force_remap_red and is_remap_red(r, g, b, a):
                # Mid remap (bright house)
                data[y * w + x] = 16 + 8
                continue
            data[y * w + x] = nearest_index((r, g, b), pal, forbid=forbid)
    out.putdata(list(data))
    return out


def indexed_frame_bbox(indexed, canvas_w: int, canvas_h: int) -> Tuple[int, int, int, int, bytes]:
    """Return (x, y, w, h, raw_pixels) for non-zero content; empty frame → 0-size."""
    from PIL import Image
    im = indexed.convert("P")
    if im.size != (canvas_w, canvas_h):
        # Paste onto canvas top-left if smaller; crop if larger
        canvas = Image.new("P", (canvas_w, canvas_h))
        canvas.putpalette(im.getpalette() or [])
        canvas.paste(im, (0, 0))
        im = canvas
    px = list(im.getdata())
    xs, ys = [], []
    for i, v in enumerate(px):
        if v != 0:
            xs.append(i % canvas_w)
            ys.append(i // canvas_w)
    if not xs:
        return 0, 0, 0, 0, b""
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    fw, fh = x1 - x0 + 1, y1 - y0 + 1
    raw = bytearray(fw * fh)
    for yy in range(fh):
        for xx in range(fw):
            raw[yy * fw + xx] = px[(y0 + yy) * canvas_w + (x0 + xx)]
    return x0, y0, fw, fh, bytes(raw)


def pack_shp_ts(frames: Sequence, canvas_w: int, canvas_h: int) -> bytes:
    """
    Pack SHP(TS) with uncompressed (raw) frame payloads.
    frames: list of PIL images (RGBA or P) or already-indexed P images.
    """
    from PIL import Image
    n = len(frames)
    header = struct.pack("<HHHH", 0, canvas_w, canvas_h, n)
    # Reserve image headers; payload after headers
    hdr_size = 8 + 24 * n
    payloads = []
    meta = []
    offset = hdr_size
    for fr in frames:
        if not isinstance(fr, Image.Image):
            raise TypeError("frame must be PIL Image")
        if fr.mode != "P":
            raise ValueError("frames must be mode P (call rgba_to_indexed first)")
        x, y, w, h, raw = indexed_frame_bbox(fr, canvas_w, canvas_h)
        meta.append((x, y, w, h, offset if raw else 0))
        payloads.append(raw)
        offset += len(raw)
    out = bytearray(header)
    for x, y, w, h, off in meta:
        # compression=0 (raw), unknowns 0
        out += struct.pack("<HHHHiiii", x, y, w, h, 0, 0, 0, off)
    for raw in payloads:
        out += raw
    return bytes(out)


# ---------------------------------------------------------------- MagicaVoxel .vox
def read_vox(path: Path) -> Tuple[List[Tuple[int, int, int, int]], List[Tuple[int, int, int]], Tuple[int, int, int]]:
    """
    Return (voxels[(x,y,z,color_idx)], palette[256] RGB 0-255, size(sx,sy,sz)).
    color_idx is 1-based Magica index; we store 0-255 palette.
    """
    data = Path(path).read_bytes()
    if data[:4] != b"VOX ":
        raise ValueError(f"not a VOX file: {path}")
    ver = struct.unpack_from("<I", data, 4)[0]
    # MAIN chunk
    pos = 8
    voxels: List[Tuple[int, int, int, int]] = []
    size = (1, 1, 1)
    # default Magica palette
    pal = [(0, 0, 0)] * 256

    def read_chunk(p: int):
        nonlocal size, voxels, pal
        if p + 12 > len(data):
            return len(data)
        cid = data[p:p + 4]
        content_size, children_size = struct.unpack_from("<II", data, p + 4)
        content = p + 12
        end_content = content + content_size
        children_end = end_content + children_size
        if cid == b"SIZE":
            sx, sy, sz = struct.unpack_from("<iii", data, content)
            size = (sx, sy, sz)
        elif cid == b"XYZI":
            n = struct.unpack_from("<I", data, content)[0]
            o = content + 4
            for i in range(n):
                x, y, z, c = data[o], data[o + 1], data[o + 2], data[o + 3]
                voxels.append((x, y, z, c))
                o += 4
        elif cid == b"RGBA":
            for i in range(256):
                r, g, b, a = data[content + i * 4: content + i * 4 + 4]
                # Magica: entry 0 unused; color index N uses palette[N-1]
                pal[i] = (r, g, b)
        cpos = end_content
        while cpos < children_end:
            cpos = read_chunk(cpos)
        return children_end

    # skip MAIN header then children
    main_content = 8 + 12
    _, children_size = struct.unpack_from("<II", data, 12)
    end = main_content + struct.unpack_from("<I", data, 12)[0]  # unused
    pos = 20  # after MAIN id+sizes
    # Actually MAIN: at 8: 'MAIN', at 12: content_size (usually 0), at 16: children_size
    content_size, children_size = struct.unpack_from("<II", data, 12)
    pos = 20 + content_size
    end = pos + children_size
    while pos < end:
        pos = read_chunk(pos)
    return voxels, pal, size


def _best_normal_index(nx: float, ny: float, nz: float) -> int:
    best, best_d = 0, -2.0
    for i, (a, b, c) in enumerate(NORMALS4):
        d = a * nx + b * ny + c * nz
        if d > best_d:
            best, best_d = i, d
    return best & 0xFF


def vox_to_section(path: Path, name: str = "body",
                   pal_unittem: Optional[Sequence[Tuple[int, int, int]]] = None):
    """Build a VxlSection-like dict from MagicaVoxel file."""
    voxels, vox_pal, (sx, sy, sz) = read_vox(path)
    if pal_unittem is None:
        pal_unittem = load_unittem_pal()
    # Map Magica color index (1..255) → unittem index
    mapped = []
    for x, y, z, c in voxels:
        # Magica stores color index; palette slot is c-1 historically
        idx = max(1, min(255, c))
        rgb = vox_pal[idx - 1] if idx >= 1 else vox_pal[0]
        if is_remap_red(*rgb, 255):
            ci = 16 + 8
        else:
            ci = nearest_index(rgb, pal_unittem, forbid={0, 1})
        # Approximate outward normal from center
        cx, cy, cz = (sx - 1) / 2, (sy - 1) / 2, (sz - 1) / 2
        vx, vy, vz = x - cx, y - cy, z - cz
        ln = math.sqrt(vx * vx + vy * vy + vz * vz) or 1.0
        nrm = _best_normal_index(vx / ln, vy / ln, vz / ln)
        mapped.append((x, y, z, ci, nrm))
    return {
        "name": name[:15],
        "size": (sx, sy, sz),
        "voxels": mapped,
        "scale": 1.0 / 12.0,
        "mins": (0.0, 0.0, 0.0),
        "maxs": (float(sx), float(sy), float(sz)),
        "normals_type": 4,
    }


def write_vxl(sections: Sequence[dict], palette: Optional[Sequence[Tuple[int, int, int]]] = None) -> bytes:
    """
    Write a minimal RA2-compatible VXL (multiple sections).
    Section body encoding matches XCC span format used by ra2lib.Vxl reader.
    """
    if palette is None:
        palette = load_unittem_pal()
    nsec = len(sections)
    # Header: "Voxel Animation\0" (16) + palcount,nsec,nsec,bodysize
    ident = b"Voxel Animation"
    ident = ident + b"\x00" * (16 - len(ident))
    # Palette in file is 6-bit
    pal_bytes = bytearray()
    for r, g, b in list(palette)[:256]:
        pal_bytes += bytes([r >> 2, g >> 2, b >> 2])
    while len(pal_bytes) < 768:
        pal_bytes += b"\x00"
    # Section name headers 28 bytes each
    name_block = bytearray()
    for sec in sections:
        nm = sec["name"].encode("latin-1")[:16]
        nm = nm + b"\x00" * (16 - len(nm))
        name_block += nm + struct.pack("<III", 0, 0, 0)

    # Build body spans per section
    bodies = []
    for sec in sections:
        sx, sy, sz = sec["size"]
        # index voxels by column (y * sx + x)
        cols = {}
        for x, y, z, col, nrm in sec["voxels"]:
            if not (0 <= x < sx and 0 <= y < sy and 0 <= z < sz):
                continue
            key = y * sx + x
            cols.setdefault(key, []).append((z, col, nrm))
        # span index table: sx*sy ints
        span_index = bytearray()
        span_data = bytearray()
        ncol = sx * sy
        for c in range(ncol):
            if c not in cols:
                span_index += struct.pack("<i", -1)
                continue
            span_index += struct.pack("<i", len(span_data))
            spans = sorted(cols[c], key=lambda t: t[0])
            # Merge contiguous z into runs
            z = 0
            i = 0
            while i < len(spans) and z < sz:
                z_next, col, nrm = spans[i]
                skip = z_next - z
                # gather run
                run = [(col, nrm)]
                i += 1
                while i < len(spans) and spans[i][0] == z_next + len(run):
                    run.append((spans[i][1], spans[i][2]))
                    i += 1
                span_data.append(skip & 0xFF)
                span_data.append(len(run) & 0xFF)
                for ccol, cnrm in run:
                    span_data.append(ccol & 0xFF)
                    span_data.append(cnrm & 0xFF)
                span_data.append(0)  # trailing byte per span (ra2lib skips 1)
                z = z_next + len(run)
            # terminator: skip remaining + count 0? reader stops at z>=sz
            if z < sz:
                span_data.append((sz - z) & 0xFF)
                span_data.append(0)
                span_data.append(0)
        bodies.append((span_index + span_data, len(span_index), len(span_data)))

    body = bytearray()
    tailers = bytearray()
    for i, sec in enumerate(sections):
        blob, index_len, _ = bodies[i]
        sstart = len(body)
        body += blob
        send = len(body)
        sdata = sstart + index_len  # span data relative to body start — reader uses body[sdata+co]
        # Actually ra2lib: body = data[body_start:body_start+bodysize]; co from span table;
        # p = sdata + co where sdata is absolute within body. So sdata field = offset of span_data within body.
        scale = float(sec.get("scale", 1.0 / 12.0))
        tf = sec.get("transform") or (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
        if len(tf) != 12:
            tf = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
        mins = sec.get("mins") or (0.0, 0.0, 0.0)
        maxs = sec.get("maxs") or (float(sec["size"][0]), float(sec["size"][1]), float(sec["size"][2]))
        sx, sy, sz = sec["size"]
        ntype = int(sec.get("normals_type", 4))
        tailers += struct.pack("<III", sstart, send, sdata)
        tailers += struct.pack("<f", scale)
        tailers += struct.pack("<12f", *tf)
        tailers += struct.pack("<3f", *mins)
        tailers += struct.pack("<3f", *maxs)
        # padded to byte 88: after 3+1+12+3+3 floats/ints = 4*3 + 4 + 48 + 12 + 12 = 12+4+48+12+12 = 88
        # Wait: III=12, f=4 → 16; 12f=48 → 64; 3f=12 → 76; 3f=12 → 88; then sx,sy,sz,ntype
        tailers += bytes([sx & 0xFF, sy & 0xFF, sz & 0xFF, ntype & 0xFF])

    bodysize = len(body)
    # remap shorts (2 bytes) after palette — often zero
    remap = struct.pack("<H", 0)
    out = bytearray()
    out += ident
    out += struct.pack("<IIII", 256, nsec, nsec, bodysize)
    out += pal_bytes
    out += remap
    out += name_block
    out += body
    out += tailers
    return bytes(out)


def write_hva_static(n_sections: int = 1, n_frames: int = 1) -> bytes:
    """Minimal HVA: identity matrices per section per frame."""
    # HVA header: "Hybrid Voxel Animation\0..." see ra2lib Hva
    # Simplified: many tools use 16-byte id + frames + sections
    ident = b"Hybrid Voxel Animation"
    # Keep compatible enough for loaders that only need section count; OpenRA2 often ignores HVA for static.
    buf = bytearray(ident[:16].ljust(16, b"\x00"))
    buf += struct.pack("<II", n_frames, n_sections)
    # unknown padding used by some writers
    buf += struct.pack("<II", 0, 0)
    ident_mat = [
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
    ]
    for _f in range(n_frames):
        for _s in range(n_sections):
            buf += struct.pack("<12f", *ident_mat)
    return bytes(buf)


def save_indexed_png(indexed, path: Path) -> None:
    """Save mode-P image as RGBA PNG using its palette (for engine PNG path)."""
    from PIL import Image
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    rgba = indexed.convert("RGBA")
    # Ensure index 0 is transparent in RGBA conversion — PIL P→RGBA uses palette alpha if set
    px = indexed.load()
    out = rgba.load()
    w, h = indexed.size
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                out[x, y] = (0, 0, 0, 0)
            elif px[x, y] == 1:
                out[x, y] = (0, 0, 0, 120)
    rgba.save(path)
