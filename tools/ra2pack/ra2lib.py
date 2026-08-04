# RA2 asset extraction library: MIX (encrypted), SHP, PAL, VXL, HVA readers.
import struct, math, os, zlib
from Crypto.Cipher import Blowfish

GAME_DIR = r"e:\AI_Generated_Projects\OpenRA2\tools\ra2pack\game"

# ---------------------------------------------------------------- RSA key
# RA2 public key blob (XCC mix_decode.cpp pubkey_str). ASN.1-ish: 0x02, len, modulus (big-endian).
_PUBKEY_B64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"

def _derive_blowfish_key(key_source: bytes) -> bytes:
    """XCC get_blowfish_key/process_predata: RSA public-key transform of key_source."""
    import base64
    blob = base64.b64decode(_PUBKEY_B64)
    assert blob[0] == 2 and len(key_source) == 80
    i = 1
    if blob[i] & 0x80:
        nlen = blob[i] & 0x7F
        keylen = int.from_bytes(blob[i + 1:i + 1 + nlen], "big")
        i += 1 + nlen
    else:
        keylen = blob[i]
        i += 1
    n = int.from_bytes(blob[i:i + keylen], "big")
    pubkey_len = n.bit_length() - 1          # XCC pubkey.len
    a = (pubkey_len - 1) // 8                # input block a+1 bytes, output a bytes
    out = b""
    pos = 0
    while a + 1 <= len(key_source) - pos:
        m = int.from_bytes(key_source[pos:pos + a + 1], "little")
        c = pow(m, 0x10001, n)
        out += (c % (1 << (8 * a))).to_bytes(a, "little")
        pos += a + 1
    return out[:56]

# ---------------------------------------------------------------- blowfish (XCC block-level decipher == plain ECB byte stream)

# ---------------------------------------------------------------- name hash (TS/RA2 = zlib crc32 of padded uppercase name)
def _ts_pad(name: str) -> bytes:
    name = name.upper()
    l = len(name)
    a = l >> 2
    if l & 3:
        name += chr(l - (a << 2))
        name += name[a << 2] * (3 - (l & 3))
    return name.encode("latin-1")

def name_hash(name: str) -> int:
    return zlib.crc32(_ts_pad(name)) & 0xFFFFFFFF

# global id -> filename DB (XCC ra2 mix description, stored next to this script)
_NAME_DB = None
def name_db() -> dict:
    global _NAME_DB
    if _NAME_DB is None:
        _NAME_DB = {}
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ra2names.txt")
        if os.path.exists(p):
            for line in open(p, "r", encoding="latin-1"):
                n = line.strip().lower()
                if n and " " not in n:
                    _NAME_DB.setdefault(name_hash(n), n)
        for extra in ["local mix database.dat", "ra2.csf", "local.mix", "conquer.mix",
                      "cache.mix", "generic.mix", "temperat.mix", "snow.mix", "urban.mix",
                      "urbann.mix", "lunar.mix", "desert.mix", "cameo.mix", "isotem.mix",
                      "isotemp.mix", "isosnow.mix", "isourb.mix", "isourbn.mix", "isodes.mix",
                      "isolun.mix", "isogen.mix", "neutral.mix", "audio.mix", "sounds.mix",
                      "speech.mix", "taunts.mix", "sidec01.mix", "sidec02.mix", "sidenc01.mix",
                      "sidenc02.mix", "sno.mix", "tem.mix", "urb.mix", "load.mix", "rules.ini",
                      "art.ini", "unittem.pal", "unitsno.pal", "uniturb.pal", "cameo.pal",
                      # Yuri's Revenge top/nested mixes (drop RA2MD.MIX / LANGMD.MIX into game/)
                      "ra2md.mix", "langmd.mix", "expandmd01.mix", "localmd.mix", "conqmd.mix",
                      "genmd.mix", "cameomd.mix", "isogenmd.mix", "audiomd.mix"]:
            _NAME_DB.setdefault(name_hash(extra), extra)
    return _NAME_DB

# ---------------------------------------------------------------- MIX reader
class MixEntry:
    __slots__ = ("id", "offset", "size")

class MixFile:
    def __init__(self, data: bytes, name: str = ""):
        self.data = data
        self.name = name
        self.index = {}      # id -> MixEntry
        self.names = {}      # lowercase filename -> id (from local mix database)
        self.body_base = 0
        self._parse()

    def _parse(self):
        d = self.data
        flags = struct.unpack_from("<I", d, 0)[0]
        pos = 0
        if flags & 0x00020000:  # encrypted
            key_src = d[4:84]
            bf_key = _derive_blowfish_key(key_src)
            cipher = Blowfish.new(bf_key, Blowfish.MODE_ECB)
            first = cipher.decrypt(d[84:92])
            count, body_size = struct.unpack_from("<HI", first, 0)
            idx_len = 6 + 12 * count
            padded = (idx_len + 7) & ~7
            rest = cipher.decrypt(d[84:84 + padded])
            count, body_size = struct.unpack_from("<HI", rest, 0)
            self.body_base = 84 + padded
            raw = rest[6:idx_len]
        elif flags & 0x00010000:  # checksum only
            count, body_size = struct.unpack_from("<HI", d, 4)
            self.body_base = 4 + 6 + 12 * count
            raw = d[10:10 + 12 * count]
        else:  # plain TD-style header
            count, body_size = struct.unpack_from("<HI", d, 0)
            self.body_base = 6 + 12 * count
            raw = d[6:6 + 12 * count]
        self.count = count
        self.body_size = body_size
        for i in range(count):
            eid, off, size = struct.unpack_from("<III", raw, i * 12)
            e = MixEntry(); e.id = eid; e.offset = off; e.size = size
            self.index[eid] = e
        # parse local mix database if present
        lmd = self.get("local mix database.dat")
        if lmd:
            self._parse_lmd(lmd)

    def _parse_lmd(self, data: bytes):
        # XCC mix database: 32-byte magic, int32 size, int32 game, then (asciiz name, u32 id)*
        if len(data) < 40 or not data[:24].startswith(b"XCC by Olaf van der Spek"):
            return
        pos = 40
        n = len(data)
        while pos < n:
            end = data.find(b"\x00", pos)
            if end < 0 or end + 5 > n:
                break
            fname = data[pos:end].decode("latin-1")
            fid = struct.unpack_from("<I", data, end + 1)[0]
            self.names[fname.lower()] = fid
            pos = end + 5

    def has(self, filename: str) -> bool:
        return name_hash(filename) in self.index

    def get(self, filename: str):
        eid = name_hash(filename)
        e = self.index.get(eid)
        if not e:
            return None
        return self.data[self.body_base + e.offset: self.body_base + e.offset + e.size]

    def get_by_id(self, eid: int):
        e = self.index.get(eid)
        if not e:
            return None
        return self.data[self.body_base + e.offset: self.body_base + e.offset + e.size]

    def looks_like_mix(self, data: bytes) -> bool:
        if data is None or len(data) < 16:
            return False
        try:
            flags = struct.unpack_from("<I", data, 0)[0]
            if flags & 0x00020000:
                return True  # encrypted nested mix (rare)
            pos = 4 if (flags & 0x00010000) else 0
            count, body_size = struct.unpack_from("<HI", data, pos)
            hdr = pos + 6 + 12 * count
            return 0 < count < 30000 and hdr + body_size <= len(data) + 32
        except Exception:
            return False

# ---------------------------------------------------------------- mix tree
class MixTree:
    """Loads top-level mixes from game dir; recursively expands nested mixes; resolves files by hash."""

    def __init__(self, game_dir: str = GAME_DIR):
        self.mixes = []   # MixFile list, BFS order (children appended after parents)
        self.by_name = {}
        for fn in sorted(os.listdir(game_dir)):
            if not fn.lower().endswith(".mix"):
                continue
            path = os.path.join(game_dir, fn)
            with open(path, "rb") as f:
                data = f.read()
            try:
                mf = MixFile(data, fn.lower())
            except Exception as ex:
                print("skip top mix", fn, ex)
                continue
            self.mixes.append(mf)
            self.by_name[fn.lower()] = mf
        self._expand_nested()

    def _looks_mix(self, data: bytes) -> bool:
        if len(data) < 16:
            return False
        try:
            mf = MixFile(data, "probe")
            return mf.count > 0
        except Exception:
            return False

    def _expand_nested(self):
        db = name_db()
        mix_names = [n for n in set(db.values()) if n.endswith(".mix")]
        i = 0
        while i < len(self.mixes):
            mf = self.mixes[i]; i += 1
            # 1) try known .mix filenames by hash
            for cn in mix_names:
                if cn == mf.name:
                    continue
                data = mf.get(cn)
                if data is None:
                    continue
                key = mf.name + "/" + cn
                if key in self.by_name:
                    continue
                if not self._looks_mix(data):
                    continue
                try:
                    child = MixFile(data, key)
                except Exception:
                    continue
                self.mixes.append(child)
                self.by_name[key] = child
            # 2) probe unnamed entries that look like mixes
            for eid, e in mf.index.items():
                if eid in db or e.size < 2000:
                    continue
                data = mf.get_by_id(eid)
                if data is None or not self._looks_mix(data):
                    continue
                key = f"{mf.name}/?{eid:08x}"
                if key in self.by_name:
                    continue
                try:
                    child = MixFile(data, key)
                except Exception:
                    continue
                self.mixes.append(child)
                self.by_name[key] = child

    def find(self, filename: str):
        """return (mixname, data) searching deepest mixes first"""
        for mf in reversed(self.mixes):
            data = mf.get(filename)
            if data is not None:
                return mf.name, data
        return None, None

# ---------------------------------------------------------------- palette
def load_pal(data: bytes):
    pal = []
    for i in range(256):
        r, g, b = data[i * 3], data[i * 3 + 1], data[i * 3 + 2]
        pal.append((r << 2, g << 2, b << 2))
    return pal

# ---------------------------------------------------------------- SHP (TS/RA2)
# file header: i16 zero, i16 cx, i16 cy, i16 c_images (8 bytes)
# image header (24 bytes): i16 x, i16 y, i16 cx, i16 cy, i32 compression, i32 unknown, i32 zero, i32 offset
# compression & 2 -> RLE (format3): per row u16 rowlen(incl. itself), tokens: nonzero=literal px, 0=transparent run (next byte=count)
class ShpFrame:
    __slots__ = ("x", "y", "w", "h", "pixels")

class Shp:
    def __init__(self, data: bytes):
        self.data = data
        _zero, self.w, self.h, self.nframes = struct.unpack_from("<HHHH", data, 0)
        self.frames = []
        for i in range(self.nframes):
            base = 8 + i * 24
            x, y, w, h = struct.unpack_from("<HHHH", data, base)
            comp = struct.unpack_from("<I", data, base + 8)[0]
            off = struct.unpack_from("<I", data, base + 20)[0]
            self.frames.append((x, y, w, h, comp, off))

    def frame_pixels(self, i: int) -> ShpFrame:
        x, y, w, h, comp, off = self.frames[i]
        f = ShpFrame(); f.x = x; f.y = y; f.w = w; f.h = h
        px = bytearray(w * h)
        if w == 0 or h == 0 or off == 0:
            f.pixels = px; return f
        d = self.data
        if comp & 2:  # RLE format3
            p = off
            for row in range(h):
                rowlen = struct.unpack_from("<H", d, p)[0]
                p += 2
                end = p + rowlen - 2
                xx = 0
                while p < end:
                    v = d[p]; p += 1
                    if v:
                        if xx < w:
                            px[row * w + xx] = v
                        xx += 1
                    else:
                        v2 = d[p]; p += 1
                        xx += v2
        else:  # raw
            px = bytearray(d[off:off + w * h])
        f.pixels = px
        return f

def remap_index(idx: int):
    if 16 <= idx <= 31:
        r = int(round(160 + (31 - idx) * 95 / 15))
        return (r, 0, 0, 255)
    return None

def shp_frame_to_rgba(frame: ShpFrame, pal, canvas=None, remap=True):
    """returns PIL image of the frame (not canvas), with remap applied.
    RA2 unittem.pal: index 0 = transparent; index 1 = shadow (palette RGB is
    bright blue and must never be drawn opaque — use translucent black)."""
    from PIL import Image
    img = Image.new("RGBA", (frame.w, frame.h), (0, 0, 0, 0))
    out = img.load()
    px = frame.pixels
    for yy in range(frame.h):
        for xx in range(frame.w):
            v = px[yy * frame.w + xx]
            if v == 0:
                continue
            if v == 1:
                out[xx, yy] = (0, 0, 0, 120)
                continue
            rm = remap_index(v) if remap else None
            if rm:
                out[xx, yy] = rm
            else:
                r, g, b = pal[v]
                out[xx, yy] = (r, g, b, 255)
    return img

# ---------------------------------------------------------------- VXL
class VxlSection:
    def __init__(self):
        self.name = ""
        self.size = (0, 0, 0)
        self.transform = None
        self.scale = 1.0
        self.mins = (0.0, 0.0, 0.0)
        self.maxs = (0.0, 0.0, 0.0)
        self.normals_type = 4  # RA2 default (tailer last byte)
        self.voxels = []   # (x,y,z,color,normal)

class Vxl:
    def __init__(self, data: bytes):
        assert data[:15] == b"Voxel Animation"
        palcount, nsec, _nsec2, bodysize = struct.unpack_from("<IIII", data, 16)
        hdr_end = 16 + 16 + 768 + 2  # id + ints + palette + remap shorts
        # section headers (28 bytes each: name16 + 3 ints)
        self.sections = []
        heads = []
        for i in range(nsec):
            name = data[hdr_end + i * 28: hdr_end + i * 28 + 16].split(b"\x00")[0].decode("latin-1")
            heads.append(name)
        # layout: headers, then BODY (bodysize), then section tailers (92 bytes each)
        body_start = hdr_end + 28 * nsec
        tailer_base = body_start + bodysize
        for i in range(nsec):
            tbase = tailer_base + i * 92
            sstart, send, sdata = struct.unpack_from("<III", data, tbase)
            scale = struct.unpack_from("<f", data, tbase + 12)[0]
            tf = struct.unpack_from("<12f", data, tbase + 16)
            mins = struct.unpack_from("<3f", data, tbase + 64)
            maxs = struct.unpack_from("<3f", data, tbase + 76)
            sx, sy, sz = data[tbase + 88], data[tbase + 89], data[tbase + 90]
            ntype = data[tbase + 91] if tbase + 91 < len(data) else 4
            sec = VxlSection()
            sec.name = heads[i]
            sec.size = (sx, sy, sz)
            sec.transform = tf
            sec.scale = scale
            sec.mins = mins
            sec.maxs = maxs
            sec.normals_type = ntype
            body = data[body_start: body_start + bodysize]
            if sstart == 0xFFFFFFFF or send == 0xFFFFFFFF or sx == 0 or sy == 0 or sz == 0:
                self.sections.append(sec)
                continue
            ncol = sx * sy
            for c in range(ncol):
                co = struct.unpack_from("<i", body, sstart + c * 4)[0]
                # span 偏移相对 span_data(sdata) 起点；co == -1 表示空列
                if co < 0 or sdata + co >= len(body):
                    continue
                x = c % sx  # XCC: j = y*cx + x（y 主序）
                y = c // sx
                # XCC 解码：z 相对累计；每体素 2 字节(色,法线)交错；每 span 后跳 1 字节；z>=sz 终止
                p = sdata + co
                z = 0
                while z < sz and p + 1 < len(body):
                    z += body[p]; p += 1
                    cnt = body[p]; p += 1
                    for k in range(cnt):
                        if p + 1 >= len(body):
                            break
                        col, nrm = body[p], body[p + 1]
                        sec.voxels.append((x, y, z, col, nrm)); p += 2
                        z += 1
                    p += 1
            self.sections.append(sec)

# ---------------------------------------------------------------- VPL + normals（原版光照）
_VPL = None
_NORMALS4 = None

def load_vpl(data: bytes):
    """voxels.vpl → (nsections, tables[sec][color]=out_color)"""
    if not data or len(data) < 16 + 768:
        return None
    first, last, nsec, _unk = struct.unpack_from("<IIII", data, 0)
    nsec = max(1, min(32, nsec))
    base = 16 + 768
    tables = []
    for s in range(nsec):
        off = base + s * 256
        tables.append(list(data[off:off + 256]))
    return {"first": first, "last": last, "n": nsec, "tables": tables}

def get_vpl():
    global _VPL
    if _VPL is None:
        try:
            t = MixTree()
            _, raw = t.find("voxels.vpl")
            _VPL = load_vpl(raw) if raw else False
        except Exception:
            _VPL = False
    return _VPL if _VPL else None

def get_normals4():
    global _NORMALS4
    if _NORMALS4 is None:
        try:
            from ra2_normals4 import NORMALS4
            _NORMALS4 = NORMALS4
        except Exception:
            _NORMALS4 = []
    return _NORMALS4

_NORMALS2 = None

def get_normals2():
    global _NORMALS2
    if _NORMALS2 is None:
        try:
            from ra2_normals2 import NORMALS2
            _NORMALS2 = NORMALS2
        except Exception:
            _NORMALS2 = []
    return _NORMALS2

def get_normals_for_type(ntype: int):
    """VXL tailer normals_type：2=TS 表，4=RA2 表；其它回退 4。"""
    if ntype == 2:
        n = get_normals2()
        if n:
            return n
    return get_normals4()

def vpl_shade_index(color: int, normal_idx: int, facing_cos: float, facing_sin: float,
                    normals_type: int = 4, light=(-0.55, -0.55, 0.63)):
    """按 normals_type 取法线表 × voxels.vpl → 调色板索引。"""
    norms = get_normals_for_type(normals_type)
    vpl = get_vpl()
    if norms and vpl:
        ni = normal_idx % len(norms)
        nx, ny, nz = norms[ni]
        rx = nx * facing_cos - ny * facing_sin
        ry = nx * facing_sin + ny * facing_cos
        rz = nz
        lx, ly, lz = light
        ln = math.sqrt(lx * lx + ly * ly + lz * lz) or 1.0
        dot = (rx * lx + ry * ly + rz * lz) / ln
        nsec = vpl["n"]
        sec = int(max(0.0, min(1.0, (dot + 1.0) * 0.5)) * (nsec - 1) + 0.5)
        sec = max(0, min(nsec - 1, sec))
        out = vpl["tables"][sec][color & 255]
        # 近白（常为索引 15）：回退原色，由面乘子做明暗，避免雪花
        if out == 15 or (not (16 <= (color & 255) <= 31) and sec > 20):
            # 用中段表或原色
            sec2 = min(sec, 14)
            out2 = vpl["tables"][sec2][color & 255]
            if out2 == 15:
                return color & 255, max(0.45, min(1.0, 0.5 + 0.5 * max(0.0, dot)))
            out = out2
        return out, 1.0
    shade = 0.72
    if norms:
        ni = normal_idx % len(norms)
        nx, ny, nz = norms[ni]
        rx = nx * facing_cos - ny * facing_sin
        ry = nx * facing_sin + ny * facing_cos
        rz = nz
        lx, ly, lz = light
        ln = math.sqrt(lx * lx + ly * ly + lz * lz) or 1.0
        dot = (rx * lx + ry * ly + rz * lz) / ln
        shade = max(0.42, min(1.0, 0.48 + 0.52 * max(0.0, dot)))
    return color & 255, shade

class Hva:
    def __init__(self, data: bytes):
        self.valid = False
        if len(data) < 24:
            return
        self.nframes, self.nsec = struct.unpack_from("<II", data, 16)
        if not (0 < self.nframes < 1024 and 0 < self.nsec < 64):
            return
        self.sec_names = []
        pos = 24
        for i in range(self.nsec):
            self.sec_names.append(data[pos:pos + 16].split(b"\x00")[0].decode("latin-1"))
            pos += 16
        need = pos + self.nframes * self.nsec * 48
        if len(data) < need:
            return
        self.mats = []
        for f in range(self.nframes):
            row = []
            for s in range(self.nsec):
                row.append(struct.unpack_from("<12f", data, pos))
                pos += 48
            self.mats.append(row)
        self.valid = True

def _apply_mat(m, x, y, z):
    return (m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])

# ---------------------------------------------------------------- VXL renderer v7
# 表面立方体面 + normals_type + VPL（近白回退）+ NEAREST
_FACE_OFFSETS = (
    (1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1),
)
_FACE_CORNERS = (
    ((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)),
    ((0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)),
    ((0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)),
    ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)),
    ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)),
)
_FACE_NORMALS = (
    (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0), (0.0, -1.0, 0.0),
    (0.0, 0.0, 1.0), (0.0, 0.0, -1.0),
)
_FACE_MUL = (0.88, 0.72, 0.92, 0.78, 1.05, 0.65)

def _phi_for_screen_alpha(alpha_deg: float) -> float:
    a = math.radians(alpha_deg)
    return math.degrees(math.atan2(2 * math.sin(a) - math.cos(a), 2 * math.sin(a) + math.cos(a)))

def _proj(x, y, z):
    return x - y, (x + y) * 0.5 - z

def _rot_z(x, y, z, cosf, sinf):
    return x * cosf - y * sinf, x * sinf + y * cosf, z

def vxl_project(vxl: Vxl, hva, facing_phi_deg: float, hva_frame: int = 0):
    cosf = math.cos(math.radians(facing_phi_deg))
    sinf = math.sin(math.radians(facing_phi_deg))
    pts = []
    zmin = 1e9
    for sec in vxl.sections:
        m = None
        if hva and hva.valid:
            try:
                hi = hva.sec_names.index(sec.name)
                m = hva.mats[min(hva_frame, hva.nframes - 1)][hi]
            except ValueError:
                m = None
        tf = sec.transform
        mins = getattr(sec, "mins", None)
        maxs = getattr(sec, "maxs", None)
        sx, sy, sz = sec.size
        has_bounds = (
            mins is not None and maxs is not None and sx > 0 and sy > 0 and sz > 0
            and (maxs[0] - mins[0]) > 0.01 and (maxs[1] - mins[1]) > 0.01 and (maxs[2] - mins[2]) > 0.01
        )
        # HVA：仅用于动画；静态定位靠 Voxel Bounds（ModEnc）。偏移过大则丢弃。
        if m is not None and has_bounds:
            # HVA 平移按 OpenRA 方式按盒尺寸缩放；静态坦克 HVA 常为 0，可忽略
            pass
        if m is not None and tf is not None and not has_bounds:
            cx, cy, cz = (s / 2 + 0.5 for s in sec.size)
            ax, ay, az = _apply_mat(tf, cx, cy, cz)
            bx, by, bz = _apply_mat(m, ax, ay, az)
            lim = 1.5 * max(sec.size)
            if abs(bx - ax) > lim or abs(by - ay) > lim or abs(bz - az) > lim:
                m = None
        ntype = getattr(sec, "normals_type", 4) or 4
        occ = {(vx, vy, vz) for (vx, vy, vz, _c, _n) in sec.voxels}

        def xform(lx, ly, lz, _m=m):
            # 网格 → 共享世界盒（炮塔叠车体、炮管在炮塔前）
            if has_bounds:
                vx = mins[0] + lx * (maxs[0] - mins[0]) / sx
                vy = mins[1] + ly * (maxs[1] - mins[1]) / sy
                vz = mins[2] + lz * (maxs[2] - mins[2]) / sz
                x1, y1, z1 = vx, vy, vz
            elif tf is not None:
                x1, y1, z1 = _apply_mat(tf, lx, ly, lz)
            else:
                x1, y1, z1 = lx, ly, lz
            if _m is not None and not has_bounds:
                x1, y1, z1 = _apply_mat(_m, x1, y1, z1)
            return _rot_z(x1, y1, z1, cosf, sinf)

        for (vx, vy, vz, c, nrm) in sec.voxels:
            cx, cy, cz = xform(vx + 0.5, vy + 0.5, vz + 0.5)
            if cz < zmin:
                zmin = cz
            col_lit, sh0 = vpl_shade_index(c, nrm, cosf, sinf, normals_type=ntype)
            face_list = []
            for fi, (dx, dy, dz) in enumerate(_FACE_OFFSETS):
                if (vx + dx, vy + dy, vz + dz) in occ:
                    continue
                fnx, fny, fnz = _FACE_NORMALS[fi]
                rnx, rny, rnz = _rot_z(fnx, fny, fnz, cosf, sinf)
                if rnx + rny + rnz <= 0.02:
                    continue
                corners = [xform(vx + a, vy + b, vz + c3) for (a, b, c3) in _FACE_CORNERS[fi]]
                face_list.append((corners, sh0 * _FACE_MUL[fi], col_lit))
            if not face_list:
                continue
            sx, sy = _proj(cx, cy, cz)
            pts.append((sx, sy, cx + cy + cz, c & 255, 1.0, face_list))
    return pts, (zmin if pts else 0.0)

def _fill_tri(zbuf, W, H, p0, p1, p2, depth, col):
    x0, y0 = p0; x1, y1 = p1; x2, y2 = p2
    minx = max(0, int(math.floor(min(x0, x1, x2))))
    maxx = min(W - 1, int(math.ceil(max(x0, x1, x2))))
    miny = max(0, int(math.floor(min(y0, y1, y2))))
    maxy = min(H - 1, int(math.ceil(max(y0, y1, y2))))
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if abs(area) < 1e-6:
        return
    for yy in range(miny, maxy + 1):
        row = yy * W
        for xx in range(minx, maxx + 1):
            w0 = (x1 - xx) * (y2 - yy) - (x2 - xx) * (y1 - yy)
            w1 = (x2 - xx) * (y0 - yy) - (x0 - xx) * (y2 - yy)
            w2 = (x0 - xx) * (y1 - yy) - (x1 - xx) * (y0 - yy)
            inside = (w0 >= 0 and w1 >= 0 and w2 >= 0) if area > 0 else (w0 <= 0 and w1 <= 0 and w2 <= 0)
            if not inside:
                continue
            i = row + xx
            cur = zbuf[i]
            if cur is None or depth > cur[0]:
                zbuf[i] = (depth, col)

def _fill_quad(zbuf, W, H, corners, depth, col):
    _fill_tri(zbuf, W, H, corners[0], corners[1], corners[2], depth, col)
    _fill_tri(zbuf, W, H, corners[0], corners[2], corners[3], depth, col)

def _rgb_of(c, shade, pal):
    if 16 <= (c & 255) <= 31:
        t = (c - 16) / 15.0
        r = int(140 + 100 * max(0.0, min(1.0, t)) * max(0.55, min(1.0, shade)))
        return (min(255, r), 0, 0, 255)
    r, g, b = pal[c & 255]
    if r + g + b > 700:
        r = g = b = 200
    bmul = max(0.38, min(1.05, shade))
    return (min(255, int(r * bmul)), min(255, int(g * bmul)), min(255, int(b * bmul)), 255)

def render_pts(pts, pal, scale: float, org_x: float, org_y: float, canvas_w: int, canvas_h: int,
               supersample: int = 2):
    from PIL import Image
    ss = max(1, supersample)
    W, H = canvas_w * ss, canvas_h * ss
    zbuf = [None] * (W * H)
    s = scale * ss
    ox = org_x * ss
    oy = org_y * ss

    def P(wx, wy, wz):
        sx, sy = _proj(wx, wy, wz)
        return (ox + sx * s, oy + sy * s)

    for item in sorted(pts, key=lambda p: p[2]):
        depth = item[2]
        face_list = item[5] if len(item) >= 6 and isinstance(item[5], list) else None
        if not face_list:
            sx, sy, depth, c, shade = item[:5]
            col = _rgb_of(c, shade, pal)
            cx = int(round(ox + sx * s)); cy = int(round(oy + sy * s))
            for yy in range(max(0, cy - 1), min(H, cy + 2)):
                row = yy * W
                for xx in range(max(0, cx - 1), min(W, cx + 2)):
                    i = row + xx
                    if zbuf[i] is None or depth > zbuf[i][0]:
                        zbuf[i] = (depth, col)
            continue
        for fi, (corners, shade, col_i) in enumerate(face_list):
            col = _rgb_of(col_i, shade, pal)
            scr = [P(*p) for p in corners]
            _fill_quad(zbuf, W, H, scr, depth + 0.001 * fi, col)

    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out = img.load()
    for yy in range(H):
        row = yy * W
        for xx in range(W):
            v = zbuf[row + xx]
            if v is not None:
                out[xx, yy] = v[1]
    if ss > 1:
        img = img.resize((canvas_w, canvas_h), Image.NEAREST)
    return img


def vxl_pal(vxl_data: bytes):
    """VXL 内嵌 768 字节调色板（6bit，左移 2）"""
    return load_pal(vxl_data[32:32 + 768])
