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
                      "art.ini", "unittem.pal", "unitsno.pal", "uniturb.pal", "cameo.pal"]:
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
    """returns PIL image of the frame (not canvas), with remap applied"""
    from PIL import Image
    img = Image.new("RGBA", (frame.w, frame.h), (0, 0, 0, 0))
    out = img.load()
    px = frame.pixels
    for yy in range(frame.h):
        for xx in range(frame.w):
            v = px[yy * frame.w + xx]
            if v == 0:
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
        self.voxels = []   # (x,y,z,color)

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
            sec = VxlSection()
            sec.name = heads[i]
            sec.size = (sx, sy, sz)
            sec.transform = tf
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
                        sec.voxels.append((x, y, z, body[p])); p += 2
                        z += 1
                    p += 1
            self.sections.append(sec)

class Hva:
    def __init__(self, data: bytes):
        self.valid = False
        # RA2 HVA: 16-byte id (often a source path, not "*HVA*"), i32 nframes, i32 nsec,
        # then nsec 16-byte names, then nframes*nsec 48-byte matrices.
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
        self.mats = []  # [frame][section] -> 12 floats
        for f in range(self.nframes):
            row = []
            for s in range(self.nsec):
                row.append(struct.unpack_from("<12f", data, pos))
                pos += 48
            self.mats.append(row)
        self.valid = True

def _apply_mat(m, x, y, z):
    # m: 12 floats, 3 rows x 4 cols
    return (m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])

# ---------------------------------------------------------------- VXL renderer v2
# 屏幕精确朝向：引擎 dir 0..7 = 东起顺时针（屏幕系）。将世界旋转角 phi 映射到屏幕角 alpha，
# 使模型 +x（voxel 前向）投影后精确指向屏幕 8 方向。
def _phi_for_screen_alpha(alpha_deg: float) -> float:
    a = math.radians(alpha_deg)
    return math.degrees(math.atan2(2 * math.sin(a) - math.cos(a), 2 * math.sin(a) + math.cos(a)))

def vxl_project(vxl: Vxl, hva, facing_phi_deg: float, hva_frame: int = 0):
    """Transform all voxels to world space, rotate about Z, return
    (pts, zmin) where pts = [(sx, sy, depth, color, bright)] in voxel units (px=1),
    using projection sx=(x-y), sy=(x+y)/2-z, camera dir (1,1,1)."""
    cosf = math.cos(math.radians(facing_phi_deg))
    sinf = math.sin(math.radians(facing_phi_deg))
    world = []
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
        # HVA 平移异常检测（如 shad 旋翼节平移 300+ 体素）：若 m*tf 把节中心甩出
        # 超过 1.5 倍节尺寸，则该节弃用 HVA，仅用 section transform
        if m is not None and tf is not None:
            cx, cy, cz = (s / 2 + 0.5 for s in sec.size)
            ax, ay, az = _apply_mat(tf, cx, cy, cz)
            bx, by, bz = _apply_mat(m, ax, ay, az)
            lim = 1.5 * max(sec.size)
            if abs(bx - ax) > lim or abs(by - ay) > lim or abs(bz - az) > lim:
                m = None
        for (x, y, z, c) in sec.voxels:
            if tf is not None:
                x1, y1, z1 = _apply_mat(tf, x + 0.5, y + 0.5, z + 0.5)
            else:
                x1, y1, z1 = x + 0.5, y + 0.5, z + 0.5
            if m is not None:
                wx, wy, wz = _apply_mat(m, x1, y1, z1)
            else:
                wx, wy, wz = x1, y1, z1
            rx = wx * cosf - wy * sinf
            ry = wx * sinf + wy * cosf
            world.append((rx, ry, wz, c))
            if wz < zmin:
                zmin = wz
    if not world:
        return [], 0.0
    # occupancy for approximate normals
    occ = set()
    for (rx, ry, wz, c) in world:
        occ.add((int(round(rx)), int(round(ry)), int(round(wz))))
    # 光源：屏幕左上 —— +z 最亮，+y（屏幕左侧面）次之，+x 最暗
    light = (0.18, 0.52, 0.84)
    ln = math.sqrt(sum(v * v for v in light))
    light = tuple(v / ln for v in light)
    pts = []
    for (rx, ry, wz, c) in world:
        ix, iy, iz = int(round(rx)), int(round(ry)), int(round(wz))
        nx = ny = nz = 0
        for dx, dy, dz in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
            if (ix + dx, iy + dy, iz + dz) not in occ:
                nx += dx; ny += dy; nz += dz
        if nx == 0 and ny == 0 and nz == 0:
            continue  # interior
        sx = rx - ry
        sy = (rx + ry) * 0.5 - wz
        depth = rx + ry + wz
        nn = math.sqrt(nx * nx + ny * ny + nz * nz)
        dot = (nx * light[0] + ny * light[1] + nz * light[2]) / nn
        shade = max(0.0, min(1.0, dot * 0.5 + 0.5))
        pts.append((sx, sy, depth, c, shade))
    return pts, zmin

def render_pts(pts, pal, scale: float, org_x: float, org_y: float, canvas_w: int, canvas_h: int,
               supersample: int = 3):
    """Rasterize projected voxels into canvas (RGBA PIL image).
    World origin maps to (org_x, org_y); scale = px per voxel unit.
    Z-buffer splat; supersampled then downscaled."""
    from PIL import Image
    ss = supersample
    W, H = canvas_w * ss, canvas_h * ss
    zbuf = [None] * (W * H)
    half = 1.02 * scale * ss  # splat half-size：体素沿 (1,-1) 方向相邻时投影相距 2*scale，需 half>=scale 才无缝
    for (sx, sy, depth, c, shade) in pts:
        rm = remap_index(c)
        if rm:
            # remap 像素必须保持 r>150 才能被引擎 remap；明暗限制在 0.72..1.0
            b = 0.72 + 0.28 * shade
            col = (min(255, int(rm[0] * b)), 0, 0, 255)
        else:
            b = 0.55 + 0.45 * shade
            r, g, bb = pal[c]
            col = (min(255, int(r * b)), min(255, int(g * b)), min(255, int(bb * b)), 255)
        cx = (org_x + sx * scale) * ss
        cy = (org_y + sy * scale) * ss
        d = depth
        x0 = int(cx - half); x1 = int(cx + half) + 1
        y0 = int(cy - half); y1 = int(cy + half) + 1
        for yy in range(y0, y1):
            if yy < 0 or yy >= H:
                continue
            row = yy * W
            for xx in range(x0, x1):
                if xx < 0 or xx >= W:
                    continue
                i = row + xx
                cur = zbuf[i]
                if cur is None or d > cur[0]:
                    zbuf[i] = (d, col)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out = img.load()
    for yy in range(H):
        row = yy * W
        for xx in range(W):
            v = zbuf[row + xx]
            if v is not None:
                out[xx, yy] = v[1]
    if ss > 1:
        img = img.resize((canvas_w, canvas_h), Image.LANCZOS)
    return img

def vxl_pal(vxl_data: bytes):
    """VXL 内嵌 768 字节调色板（6bit，左移 2）"""
    return load_pal(vxl_data[32:32 + 768])
