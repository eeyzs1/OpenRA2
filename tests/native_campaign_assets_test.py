"""Validate mission text against original CSF bytes, including malformed records."""
import configparser
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/ra2pack"))
from gen_native_presentation import read_csf, native_actions
from ra2lib import MixTree


def main():
    folder = ROOT / "assets/maps/native/all01t"
    if not (folder / "strings.ini").exists():
        print("NATIVE CAMPAIGN TEXT SKIP: export original scene and text")
        return 77
    tree = MixTree()
    source, data = tree.find("ra2.csf")
    original = read_csf(data)
    report = json.loads((folder / "strings-report.json").read_text(encoding="utf-8"))
    assert report["source"] == source and report["sha256"] == hashlib.sha256(data).hexdigest()
    texts = configparser.ConfigParser(interpolation=None, delimiters=("=",));texts.optionxform = str
    texts.read(folder / "strings.ini", encoding="utf-8")
    scene = configparser.ConfigParser(interpolation=None);scene.optionxform = str
    scene.read(folder / "scene.ini", encoding="utf-8")
    labels = {args[1].lower() for kind, args in native_actions(scene) if kind == 11}
    labels |= {"name:all01", "brief:all01", "loadbrief:all01"}
    assert set(texts["Text"]) == set(report["labels"]) == labels and len(labels) == 24
    for key, value in texts["Text"].items():
        decoded, index = "", 0
        while index < len(value):
            if value[index] == "\\" and index + 1 < len(value):
                index += 1
                decoded += {"n": "\n", "s": ";", "h": "#"}.get(value[index], value[index])
            else:
                decoded += value[index]
            index += 1
        assert len(set(original[key])) == 1 and decoded == original[key][0], key
    malformed = [data[:length] for length in (0, 23, 31, len(data) - 1)]
    malformed += [data + b"trailing", data[:8] + struct.pack("<I", 100001) + data[12:]]
    malformed += [data[:28] + struct.pack("<I", 0xffffffff) + data[32:]]
    for broken in malformed:
        try:
            read_csf(broken)
        except (ValueError, UnicodeError):
            continue
        raise AssertionError("malformed CSF was accepted")
    print(f"NATIVE CAMPAIGN TEXT PASS labels={len(labels)} source_exact=1 malformed_rejected={len(malformed)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
