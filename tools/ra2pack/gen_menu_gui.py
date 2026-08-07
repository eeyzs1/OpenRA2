# Extract RA2 main-menu / dialog chrome into assets/gui/menu/ (local-only, copyrighted).
# Requires tools/ra2pack/game/ MIX tree. No BIK decode — title/dialog stills only.
import os, sys, struct
from ra2lib import MixTree, Shp, shp_frame_to_rgba
from PIL import Image

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
    pal = load_pal(T, ("dialog.pal", "dialogn.pal", "dialogy.pal", "unittem.pal", "cameo.pal"))

    # Title / campaign / dialog stills
    for n in (
        "title.pcx",
        "titlelg.pcx",
        "titlemd.pcx",
        "titlesm.pcx",
        "campaign.pcx",
        "bkgdlg.pcx",
        "aloadlg.pcx",
        "rloadlg.pcx",
        "dlgsysa.pcx",
        "dlgsysi.pcx",
        "mpscore.pcx",
        "wdtbkbtn.pcx",
    ):
        save_pcx(T, n)

    # Dialog SHP panels
    for n in (
        "bkgdlg.shp",
        "fsbkgdlg.shp",
        "pudlgbga.shp",
        "pudlgbgs.shp",
        "pudlgbgn.shp",
        "pudlgbgy.shp",
        "sdbtnbkgd.shp",
        "sdbtnanm.shp",
        "optbtn.shp",
        "ebtn-up.shp",
        "ebtn-dn.shp",
    ):
        save_shp(T, n, pal)

    for i in range(12):
        save_shp(T, "button%02d.shp" % i, pal)

    extract_bik(T, "ra2ts_l.bik", "ra2ts_l")

    # Manifest for the game loader
    man = os.path.join(OUT, "manifest.txt")
    with open(man, "w", encoding="utf-8") as f:
        for dirpath, _, files in os.walk(OUT):
            for fn in sorted(files):
                if fn.endswith((".png", ".jpg", ".ini")):
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


if __name__ == "__main__":
    main()
