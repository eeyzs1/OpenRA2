# Extract RA2 main-menu / dialog chrome into assets/gui/menu/ (local-only, copyrighted).
# Requires tools/ra2pack/game/ MIX tree. Decodes title stills, shell PCX, dialog SHP, fonts/pals.
# BIK: ra2ts_l via ffmpeg.
import os, sys, struct
from ra2lib import MixTree, Shp, shp_frame_to_rgba
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "assets", "gui", "menu")
os.makedirs(OUT, exist_ok=True)


def load_pal(T, names):
    for n in names:
        _, raw = T.find(n)
        if raw and len(raw) >= 768:
            pal, mx = [], max(raw[:768])
            for i in range(256):
                r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
                if mx <= 63:
                    r, g, b = r * 4, g * 4, b * 4
                pal.append((r, g, b))
            print("palette", n)
            return pal
    return [(i, i, i) for i in range(256)]


def decode_pcx(data: bytes) -> Image.Image:
    """Decode ZSoft PCX (8-bit indexed or 24-bit)."""
    if len(data) < 128 or data[0] != 10:
        raise ValueError("not a PCX")
    bits = data[3]
    xmin, ymin, xmax, ymax = struct.unpack_from("<HHHH", data, 4)
    w, h = xmax - xmin + 1, ymax - ymin + 1
    nplanes = data[65]
    bpl = struct.unpack_from("<H", data, 66)[0]
    # RLE decode from offset 128
    expected = bpl * nplanes * h
    out = bytearray()
    i = 128
    while i < len(data) and len(out) < expected:
        b = data[i]
        i += 1
        if (b & 0xC0) == 0xC0:
            cnt = b & 0x3F
            if i >= len(data):
                break
            val = data[i]
            i += 1
            out.extend([val] * cnt)
        else:
            out.append(b)
    if len(out) < expected:
        out.extend([0] * (expected - len(out)))

    if bits == 8 and nplanes == 1:
        # VGA palette at end: 0x0C + 768 bytes
        pal = [(0, 0, 0)] * 256
        if len(data) >= 769 and data[-769] == 0x0C:
            for pi in range(256):
                o = len(data) - 768 + pi * 3
                pal[pi] = (data[o], data[o + 1], data[o + 2])
        img = Image.new("RGBA", (w, h))
        px = img.load()
        for y in range(h):
            row = out[y * bpl : y * bpl + w]
            for x in range(w):
                r, g, b = pal[row[x]]
                px[x, y] = (r, g, b, 255)
        return img

    if bits == 8 and nplanes == 3:
        img = Image.new("RGBA", (w, h))
        px = img.load()
        for y in range(h):
            base = y * bpl * 3
            for x in range(w):
                r = out[base + x]
                g = out[base + bpl + x]
                b = out[base + 2 * bpl + x]
                px[x, y] = (r, g, b, 255)
        return img

    raise ValueError("unsupported PCX bits=%d planes=%d" % (bits, nplanes))


def save_pcx(T, name, out_name=None):
    _, raw = T.find(name)
    if not raw:
        print("MISS", name)
        return False
    try:
        img = decode_pcx(raw)
    except Exception as e:
        print("FAIL decode", name, e)
        return False
    path = os.path.join(OUT, (out_name or name.replace(".pcx", "")) + ".png")
    img.save(path)
    print("saved", path, img.size)
    return True


def save_shp(T, name, pal, prefix=None, shadow=False):
    _, raw = T.find(name)
    if not raw:
        print("MISS", name)
        return 0
    shp = Shp(raw)
    stem = prefix or name.replace(".shp", "")
    n = 0
    for i in range(shp.nframes):
        fr = shp.frame_pixels(i)
        if fr is None or fr.w <= 0 or fr.h <= 0:
            continue
        try:
            img = shp_frame_to_rgba(fr, pal, canvas=(shp.w, shp.h), remap=False)
        except Exception as e:
            print("skip frame", name, i, e)
            continue
        if not isinstance(img, Image.Image):
            im = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
            px = im.load()
            for yy in range(fr.h):
                for xx in range(fr.w):
                    v = fr.pixels[yy * fr.w + xx]
                    if v == 0:
                        continue
                    if shadow and v == 1:
                        px[fr.x + xx, fr.y + yy] = (0, 0, 0, 120)
                        continue
                    r, g, b = pal[v]
                    px[fr.x + xx, fr.y + yy] = (r, g, b, 255)
            img = im
        if not isinstance(img, Image.Image) or img.size[0] <= 0 or img.size[1] <= 0:
            continue
        if img.getbbox() is None:
            continue
        # 仅清掉极边缘 1px 的近白碎屑；勿擦真边框（旧 4px/420 阈值会伤 sdmpbtn）
        if stem.startswith("sdmpbtn") or stem in ("diplobtn", "optbtn", "dropdown"):
            px = img.load()
            w, h = img.size
            for y in range(h):
                for x in range(w):
                    if x > 0 and x < w - 1 and y > 0 and y < h - 1:
                        continue
                    r, g, b, a = px[x, y]
                    if a and r > 240 and g > 240 and b > 230:
                        px[x, y] = (0, 0, 0, 0)
        path = os.path.join(OUT, "%s_%02d.png" % (stem, i))
        try:
            img.save(path)
        except Exception as e:
            print("FAIL save", path, e, "size", img.size)
            continue
        n += 1
    print("saved", stem, n, "frames", shp.w, "x", shp.h)
    return n


def main():
    T = MixTree()
    pal_dlg = load_pal(T, ("dialog.pal", "dialogn.pal", "dialogy.pal", "unittem.pal", "cameo.pal"))
    pal_ui = load_pal(T, ("uibkgd.pal", "dialog.pal"))
    pal_ui_y = load_pal(T, ("uibkgdy.pal", "uibkgd.pal", "dialog.pal"))
    pal_fs = load_pal(T, ("fsbkgdlg.pal", "dialog.pal"))
    pal_side = load_pal(T, ("sidebar.pal", "shell.pal", "dialog.pal"))

    # Title / campaign / dialog stills (PCX)
    for n in (
        "title.pcx",
        "titlelg.pcx",
        "titlemd.pcx",
        "titlesm.pcx",
        "campaign.pcx",
        "bkgdlg.pcx",
        "bkgdmd.pcx",
        "bkgdsm.pcx",
        "aloadlg.pcx",
        "rloadlg.pcx",
        "dlgsysa.pcx",
        "dlgsysi.pcx",
        "mpscore.pcx",
        "wdtbkbtn.pcx",
        # 选项/读档/联机壳层底图（左视口 + 右 PCB 侧栏）
        "load.pcx",
        "multi.pcx",
    ):
        save_pcx(T, n)

    # Allied / Yuri pause & dialog panels（uibkgd 调色）
    for n, p in (
        ("bkgdlg.shp", pal_ui),
        ("bkgdmd.shp", pal_ui),
        ("bkgdsm.shp", pal_ui),
        ("bkgdlgy.shp", pal_ui_y),
        ("bkgdmdy.shp", pal_ui_y),
        ("bkgdsmy.shp", pal_ui_y),
        ("pudlgbga.shp", pal_dlg),
        ("pudlgbgs.shp", pal_dlg),
        ("pudlgbgn.shp", pal_dlg),
        ("pudlgbgy.shp", pal_dlg),
    ):
        save_shp(T, n, p)

    # 全屏菜单装饰板 / 按钮 — 侧栏按钮用 shell.pal 才有原作红/琥珀内光
    pal_btn = load_pal(T, ("shell.pal", "dialog.pal"))
    for n in (
        "fsbkgdlg.shp",
        "fsbkgdsm.shp",
        "diplobtn.shp",
        "optbtn.shp",
        "ebtn-up.shp",
        "ebtn-dn.shp",
        "dropdown.shp",
        "credits.shp",
    ):
        save_shp(T, n, pal_dlg)
    for n in ("sdbtnbkgd.shp", "sdbtnanm.shp", "sdmpbtn.shp"):
        save_shp(T, n, pal_btn)

    # 局内侧栏段（sidebar.pal；菜单侧栏拼装备用）
    for n in ("side1.shp", "side2.shp", "side3.shp", "addon.shp", "tab01.shp", "tab02.shp", "tab03.shp", "pips.shp"):
        save_shp(T, n, pal_side)

    for i in range(12):
        save_shp(T, "button%02d.shp" % i, pal_dlg)

    # 调色板原文（调试/再解码）
    pal_dir = os.path.join(OUT, "pal")
    os.makedirs(pal_dir, exist_ok=True)
    for pn in ("dialog.pal", "dialogn.pal", "dialogy.pal", "shell.pal", "fsbkgdlg.pal",
               "uibkgd.pal", "uibkgdy.pal", "sidebar.pal"):
        _, raw = T.find(pn)
        if raw and len(raw) >= 768:
            path = os.path.join(pal_dir, pn)
            with open(path, "wb") as f:
                f.write(raw[:768])
            print("saved", path)

    # 原作 Unicode 点阵字库（运行时优先于系统 TTF）
    fnt_dir = os.path.join(OUT, "fonts")
    os.makedirs(fnt_dir, exist_ok=True)
    for fn in ("game.fnt", "12metfnt.fnt", "grad6fnt.fnt"):
        _, raw = T.find(fn)
        if raw:
            path = os.path.join(fnt_dir, fn)
            with open(path, "wb") as f:
                f.write(raw)
            print("saved", path, len(raw))

    extract_country_flags(T, load_pal(T, ("unittem.pal", "dialog.pal", "shell.pal")))
    extract_faction_icons(T, load_pal(T, ("dialog.pal", "unittem.pal", "cameo.pal")))

    extract_bik(T, "ra2ts_l.bik", "ra2ts_l")

    # Manifest for the game loader
    man = os.path.join(OUT, "manifest.txt")
    with open(man, "w", encoding="utf-8") as f:
        for dirpath, _, files in os.walk(OUT):
            for fn in sorted(files):
                if fn.endswith((".png", ".jpg", ".ini", ".pal", ".fnt")):
                    rel = os.path.relpath(os.path.join(dirpath, fn), OUT).replace("\\", "/")
                    f.write(rel + "\n")
    n_img = sum(1 for dirpath, _, files in os.walk(OUT) for fn in files if fn.endswith((".png", ".jpg")))
    print("done", OUT, "images", n_img)


def extract_bik(T, name, stem):
    """Decode Bink menu video to JPEG sequence via ffmpeg (15fps original)."""
    import subprocess, shutil, tempfile
    _, raw = T.find(name)
    if not raw:
        print("MISS", name)
        return
    out_dir = os.path.join(OUT, stem)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".bik", delete=False) as tmp:
        tmp.write(raw)
        bik_path = tmp.name
    pattern = os.path.join(out_dir, "f%04d.jpg")
    cmd = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-i", bik_path,
        "-vf", "fps=15",
        "-q:v", "5",
        pattern,
    ]
    try:
        subprocess.check_call(cmd)
    except Exception as e:
        print("FAIL ffmpeg", name, e)
        try:
            os.unlink(bik_path)
        except OSError:
            pass
        return
    try:
        os.unlink(bik_path)
    except OSError:
        pass
    frames = sorted(fn for fn in os.listdir(out_dir) if fn.endswith(".jpg"))
    meta = os.path.join(out_dir, "meta.ini")
    with open(meta, "w", encoding="utf-8") as f:
        f.write("[bik]\n")
        f.write("file=%s\n" % name)
        f.write("fps=15\n")
        f.write("frames=%d\n" % len(frames))
        if frames:
            from PIL import Image as _Im
            im = _Im.open(os.path.join(out_dir, frames[0]))
            f.write("width=%d\n" % im.size[0])
            f.write("height=%d\n" % im.size[1])
    print("saved BIK", name, "->", out_dir, "frames", len(frames))


def extract_country_flags(T, pal):
    """Crop ~28×28 lobby flag icons from causfgl_a.shp etc. (frame 0 compact bbox)."""
    flag_dir = os.path.join(OUT, "flags")
    os.makedirs(flag_dir, exist_ok=True)
    mapping = {
        "america": "causfgl",
        "korea": "caskfgl",
        "france": "cafrfgl",
        "germany": "cagefgl",
        "uk": "caukfgl",
        "russia": "carufgl",
        "cuba": "cacufgl",
        "libya": "calbfgl",
        "iraq": "cairfgl",
        # china：不复用朝鲜旗；下方单独画五星红旗
        "yuri": "cunkfgl",
    }
    for key, stem in mapping.items():
        raw = None
        for name in (stem + "_a.shp", stem + ".shp"):
            _, raw = T.find(name)
            if raw:
                break
        if not raw:
            print("MISS flag", key, stem)
            continue
        shp = Shp(raw)
        saved = False
        for i in range(min(shp.nframes, 8)):
            img = shp_frame_to_rgba(shp.frame_pixels(i), pal, canvas=(shp.w, shp.h), remap=False)
            bb = img.getbbox()
            if not bb:
                continue
            bw, bh = bb[2] - bb[0], bb[3] - bb[1]
            if 8 <= bw <= 80 and 8 <= bh <= 80:
                crop = img.crop(bb)
                path = os.path.join(flag_dir, key + ".png")
                crop.save(path)
                print("saved flag", path, crop.size, "frame", i)
                saved = True
                break
        if not saved:
            print("FAIL flag crop", key)

    # 中国：原作无国旗 SHP；画简易五星红旗（与「中国」文案一致，勿用朝鲜旗）
    import math
    cn = Image.new("RGBA", (28, 28), (0, 0, 0, 0))
    cpx = cn.load()
    for y in range(4, 24):
        wave = int(1.5 * math.sin((y - 4) / 3.0))
        for x in range(3 + wave, 25 + wave):
            if 0 <= x < 28:
                shade = min(252, 200 + (x % 5) * 8)
                cpx[x, y] = (shade, 20, 20, 255)
    for y, x in [(8, 7), (9, 6), (9, 7), (9, 8), (10, 7), (8, 6), (8, 8), (10, 6), (10, 8), (7, 7), (11, 7)]:
        if 0 <= x < 28 and 0 <= y < 28:
            cpx[x, y] = (252, 220, 40, 255)
    for cx, cy in [(14, 7), (16, 10), (14, 13), (11, 15)]:
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                if abs(dx) + abs(dy) <= 1:
                    x, y = cx + dx, cy + dy
                    if 0 <= x < 28 and 0 <= y < 28:
                        cpx[x, y] = (252, 220, 40, 255)
    cn_path = os.path.join(flag_dir, "china.png")
    cn.save(cn_path)
    print("saved flag", cn_path, cn.size, "procedural PRC")


def extract_faction_icons(T, pal):
    """Lobby faction plaques: obsalli / obssovi / obsyuri (+ china/random helpers)."""
    fac_dir = os.path.join(OUT, "factions")
    os.makedirs(fac_dir, exist_ok=True)
    mapping = {
        "allies": "obsalli.shp",
        "soviet": "obssovi.shp",
        "yuri": "obsyuri.shp",
    }
    for key, name in mapping.items():
        _, raw = T.find(name)
        if not raw:
            print("MISS faction", key, name)
            continue
        shp = Shp(raw)
        img = shp_frame_to_rgba(shp.frame_pixels(0), pal, canvas=(shp.w, shp.h), remap=False)
        bb = img.getbbox()
        if bb:
            img = img.crop(bb)
        path = os.path.join(fac_dir, key + ".png")
        img.save(path)
        print("saved faction", path, img.size)

    # 中国：无专用观察者徽，用国旗放大作阵营列占位
    china_flag = os.path.join(OUT, "flags", "china.png")
    if os.path.isfile(china_flag):
        im = Image.open(china_flag).convert("RGBA")
        im = im.resize((48, 48), Image.NEAREST)
        path = os.path.join(fac_dir, "china.png")
        im.save(path)
        print("saved faction", path, im.size)

    # 随机：暗红底 + 红框 + 双向箭头（勿写 ???；界面已有「随机」文案）
    rnd = Image.new("RGBA", (48, 36), (16, 6, 6, 255))
    d = ImageDraw.Draw(rnd)
    d.rectangle([0, 0, 47, 35], outline=(200, 48, 36, 255))
    d.polygon([(10, 18), (18, 10), (18, 26)], fill=(220, 48, 36, 255))
    d.polygon([(38, 18), (30, 10), (30, 26)], fill=(220, 48, 36, 255))
    d.rectangle([20, 14, 28, 22], fill=(255, 180, 60, 255))
    path = os.path.join(fac_dir, "random.png")
    rnd.save(path)
    print("saved faction", path, rnd.size)


if __name__ == "__main__":
    main()
