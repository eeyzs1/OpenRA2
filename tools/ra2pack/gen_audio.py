# RA2 audio.bag → assets/sfx/*.wav（按 IDX flags/ChunkSize：PCM 或 IMA ADPCM）
# 用法: python gen_audio.py
import os, struct, wave
from ra2lib import MixTree

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "assets", "sfx")
os.makedirs(OUT, exist_ok=True)

# 引擎 Sfx 名 → bag 样本（优先第一命中）
MAP = {
    "shot": ["igiat1a", "igiat1b", "igiat1c"],
    "cannon": ["vgriatta", "vgriattb", "vgriattc"],
    "mirage": ["vmiratta"],
    "rhino": ["vrhiatta"],
    "apoc": ["vapoat1a"],
    "flak": ["bflaatta", "bflaattb", "bflaattc"],
    "missile": ["irocatta", "irocata", "bpatatta"],
    "explosion": ["gexp01a", "gexp05a", "gexp06a"],
    "bigexplosion": ["gexp10a", "gexp11a", "gexpapoa"],
    "tesla": ["btesat1a", "btesat2a", "itesatta"],
    "prism": ["bpriat1a", "vpristaa"],
    "click": ["umenucl1", "utab"],
    "place": ["uplace"],
    "ready": ["uqeue", "umessage"],
    "cash": ["ucredup", "ubonus", "goreupa"],
    "alarm": ["uradarof", "ualarm"],
    "deploy": ["uchev", "udeploy", "ubldup", "uplace"],
    "sell": ["usell", "ucreddn"],
    "nukelaunch": ["snukexpl", "sweastra"],
    "nukeblast": ["snukexpl", "gexp10a"],
    "lightning": ["igenzapa", "igenmelc", "sweastra"],
    "storm": ["sweastra", "sweastrb", "igenzapa"],
    "ironcurtain": ["sweaintr", "bclovata"],
    "swready": ["uswready", "umessage", "uradaron"],
    "crush": ["gexpcraa", "gexpbara"],
    "eva": ["umessage", "uradaron"],
    "navalcannon": ["vgriatta", "vsubatta"],
    "torpedo": ["vsubatta", "irocatta"],
    # dig：短促矿砂声；vorehara 解码后偏长会成噪音墙，优先 borerefa/goreupa
    "dig": ["borerefa", "goreupa", "vorehara"],
}

# 战斗开火类：解码后截断，避免 bag 样本过长叠成噪音墙
ATTACK_SFX = {
    "shot", "cannon", "mirage", "rhino", "apoc", "flak", "missile",
    "tesla", "prism", "navalcannon", "torpedo",
}

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
INDEX = [-1,-1,-1,-1,2,4,6,8] * 2

# IDX flags：bit0=stereo, bit1=PCM, bit2=?, bit3=IMA ADPCM
FLAG_STEREO = 1
FLAG_PCM = 2
FLAG_ADPCM = 8

def dec_nibble(nibble, idx, pred):
    step = STEP[idx]
    diff = step >> 3
    if nibble & 1: diff += step >> 2
    if nibble & 2: diff += step >> 1
    if nibble & 4: diff += step
    pred = pred + diff if (nibble & 8) == 0 else pred - diff
    pred = max(-32768, min(32767, pred))
    idx = max(0, min(88, idx + INDEX[nibble]))
    return pred, idx

def decode_ima_blocks(data, block=512):
    """Westwood audio.bag IMA ADPCM：块长由 IDX ChunkSize 决定（mono 常 512，stereo 1024）。
    用错块长会把半块当新头 → 噪声。"""
    out = []
    for i in range(0, len(data), block):
        chunk = data[i:i + block]
        if len(chunk) < 4:
            break
        pred = struct.unpack_from("<h", chunk, 0)[0]
        idx = min(88, chunk[2])
        out.append(pred)
        for b in chunk[4:]:
            for nibble in (b & 0xF, (b >> 4) & 0xF):
                pred, idx = dec_nibble(nibble, idx, pred)
                out.append(pred)
    return out

def decode_pcm16(data, stereo=False):
    """未压缩 PCM s16le；立体声取左声道。"""
    n = len(data) // 2
    samples = list(struct.unpack_from(f"<{n}h", data, 0))
    if stereo and len(samples) >= 2:
        samples = samples[0::2]
    return samples

def save_wav(path, samples, rate):
    if not samples:
        return False
    peak = max(1, max(abs(s) for s in samples))
    scale = min(1.0, 28000.0 / peak)
    pcm = b"".join(struct.pack("<h", int(s * scale)) for s in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setframerate(rate)
        w.setsampwidth(2)
        w.writeframes(pcm)
    return True

def main():
    T = MixTree()
    _, idx = T.find("audio.idx")
    _, bag = T.find("audio.bag")
    if not idx or not bag:
        print("FAIL: audio.idx/bag missing")
        return 1
    nul = bytes([0])
    entries = {}
    off = 12
    # Name[16] + Offset + Size + Rate + Flags + ChunkSize = 36
    while off + 36 <= len(idx):
        name = idx[off:off + 16].split(nul)[0].decode("ascii", "ignore").lower()
        o, sz, rate, flags, chunk = struct.unpack_from("<IIIII", idx, off + 16)
        entries[name] = (o, sz, rate, flags, chunk)
        off += 36
    print(f"bag entries: {len(entries)}")

    ok = skip = 0
    for sfx, cands in MAP.items():
        hit = next((c for c in cands if c in entries), None)
        if not hit:
            print(f"  MISS {sfx} candidates={cands}")
            skip += 1
            continue
        o, sz, rate, flags, chunk = entries[hit]
        raw = bag[o:o + sz]
        stereo = bool(flags & FLAG_STEREO)
        if flags & FLAG_ADPCM:
            block = chunk if chunk else (1024 if stereo else 512)
            samples = decode_ima_blocks(raw, block=block)
            kind = f"IMA block={block}"
        elif flags & FLAG_PCM:
            samples = decode_pcm16(raw, stereo=stereo)
            kind = "PCM16"
        else:
            # 兜底：多数战斗音为 IMA；块长优先 IDX
            block = chunk if chunk else (1024 if stereo else 512)
            samples = decode_ima_blocks(raw, block=block)
            kind = f"fallback-IMA block={block} flags={flags}"
        rate = rate or 22050
        if sfx == "dig":
            maxn = int(0.22 * rate)
        elif sfx in ATTACK_SFX:
            maxn = int(0.35 * rate)
        else:
            maxn = 0
        if maxn and len(samples) > maxn:
            samples = samples[:maxn]
        path = os.path.join(OUT, f"{sfx}.wav")
        if save_wav(path, samples, rate):
            print(f"  OK {sfx} <- {hit} ({kind}, {len(samples)/max(1,rate):.2f}s)")
            ok += 1
        else:
            print(f"  FAIL {sfx}")
            skip += 1
    print(f"== done: {ok} written, {skip} skipped -> {OUT}")
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
