"""Export the installed original mission's CSF text, preserving source labels."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from ra2lib import MixTree
from gen_native_maps import ROOT


def read_csf(data):
    if len(data) < 24 or data[:4] != b" FSC":
        raise ValueError("invalid CSF header")
    _, version, labels, strings, reserved, language = struct.unpack_from("<6I", data)
    if version not in (2, 3) or labels > 100000 or strings > 100000:
        raise ValueError("unsupported or oversized CSF")
    position, count, result = 24, 0, {}

    def take(length):
        nonlocal position
        if length < 0 or length > len(data) - position:
            raise ValueError("truncated CSF record")
        value = data[position:position + length]
        position += length
        return value

    def number():
        return struct.unpack("<I", take(4))[0]

    for _ in range(labels):
        if take(4) != b" LBL":
            raise ValueError("invalid CSF label")
        values, length = number(), number()
        if not 1 <= values <= 16 or not 1 <= length <= 512:
            raise ValueError("invalid CSF label size")
        key = take(length).decode("ascii").lower()
        texts = []
        for _ in range(values):
            tag, length = take(4), number()
            if tag not in (b" RTS", b"WRTS") or length > 65536:
                raise ValueError("invalid CSF string")
            texts.append(bytes(value ^ 255 for value in take(length * 2)).decode("utf-16le"))
            if tag == b"WRTS":
                take(number())
            count += 1
        # Some installed localization files repeat unrelated UI labels. Keep
        # every alternative so an ambiguous requested mission label is refused.
        result.setdefault(key, []).extend(texts)
    if position != len(data) or count != strings:
        raise ValueError("CSF header/record counts disagree")
    return result


def native_actions(scene):
    for line in scene["NativeEvent.actions"].values():
        values = line.split(",")
        count = int(values[0])
        if len(values) != 1 + count * 8:
            raise ValueError("invalid original action record")
        for start in range(1, len(values), 8):
            yield int(values[start]), values[start + 1:start + 8]


def export_text(tree, scene, target):
    source, data = tree.find("ra2.csf")
    if not data:
        raise FileNotFoundError("original ra2.csf")
    texts = read_csf(data)
    labels = {"name:all01", "brief:all01", "loadbrief:all01"}
    for kind, args in native_actions(scene):
        if kind == 11:
            labels.add(args[1].lower())
    missing = labels - texts.keys()
    if missing:
        raise ValueError("missing original mission labels: " + repr(sorted(missing)))
    lines = ["; Original localized CSF text. Backslash escapes are decoded by the campaign loader.", "[Text]"]
    for key in sorted(labels):
        if len(set(texts[key])) != 1:
            raise ValueError("ambiguous original mission label: " + key)
        value = texts[key][0].replace("\\", "\\\\").replace("\r", "").replace("\n", "\\n").replace(";", "\\s").replace("#", "\\h")
        line = key + "=" + value
        if len(line.encode("utf-8")) >= 1000:
            raise ValueError("mission text exceeds INI record capacity: " + key)
        lines.append(line)
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    (target / "strings.ini").write_text("\n".join(lines) + "\n", encoding="utf-8")
    report = {"source": source, "sha256": hashlib.sha256(data).hexdigest(), "labels": sorted(labels)}
    (target / "strings-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    import configparser
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", type=Path, default=ROOT / "assets/maps/native/all01t/scene.ini")
    args = parser.parse_args()
    ini = configparser.ConfigParser(interpolation=None);ini.optionxform = str
    ini.read(args.scene, encoding="utf-8")
    report = export_text(MixTree(), ini, args.scene.parent)
    print("NATIVE MISSION TEXT labels=" + str(len(report["labels"])))
