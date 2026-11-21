"""Check actual native speech against FFmpeg's independent IMA WAV decoder."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/ra2pack"))
from ra2lib import MixTree


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("UNIT_VOICE_CODEC SKIP: FFmpeg is required for independent decoding")
        return 77
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / "assets/sfx/voices/source_report.json").read_text(encoding="utf-8"))
    tree = MixTree()
    indexes, bags = dict(tree.find_all("audio.idx")), dict(tree.find_all("audio.bag"))
    results = []
    for clip in ("ra2_igisea", "ra2_igimoa", "ra2_igiata", "ra2_iconsea", "yr_vslasea", "yr_vmcysea"):
        record = manifest["samples"][clip]
        index, bag = indexes[record["source"]], bags[record["source"]]
        entry = None
        for pos in range(12, len(index), 36):
            name = index[pos:pos + 16].split(b"\0", 1)[0].decode("ascii").lower()
            if name == record["sample"]:
                entry = struct.unpack_from("<IIIII", index, pos + 16)
                break
        assert entry is not None, clip
        start, size, rate, flags, block = entry
        raw = bag[start:start + size]
        assert hashlib.sha256(raw).hexdigest() == record["native_sha256"], clip
        assert flags & 8 and not flags & 1 and block >= 4, (clip, flags, block)
        samples_per_block = (block - 4) * 2 + 1
        fmt = struct.pack("<HHIIHHHH", 17, 1, rate, rate * block // samples_per_block, block, 4, 2, samples_per_block)
        body = (b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" + struct.pack("<II", 4, record["frames"])
                + b"data" + struct.pack("<I", len(raw)) + raw)
        encoded, decoded = output / (clip + "-native-adpcm.wav"), output / (clip + "-ffmpeg.pcm")
        encoded.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
        subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(encoded), "-f", "s16le", str(decoded)],
                       check=True, capture_output=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        with wave.open(str(ROOT / "assets/sfx/voices" / (clip + ".wav")), "rb") as reader:
            assert (reader.getnchannels(), reader.getsampwidth(), reader.getframerate(), reader.getnframes()) == (1, 2, rate, record["frames"])
            exported = reader.readframes(reader.getnframes())
        independent = decoded.read_bytes()
        assert exported == independent, f"{clip}: native PCM differs from independent decoder"
        results.append({"clip": clip, "source": record["source"], "frames": len(exported) // 2,
                        "rate": rate, "pcm_sha256": hashlib.sha256(independent).hexdigest(), "exact": True})
        print(f"UNIT_VOICE_CODEC PASS {clip} frames={len(exported) // 2} rate={rate} exact=1")
    (output / "comparison.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"UNIT_VOICE_CODEC SUMMARY clips={len(results)} exact={len(results)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
