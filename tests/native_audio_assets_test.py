"""Verify restored music/dialogue against the actual local MIX source bytes."""
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/ra2pack"))
from ra2lib import MixTree
from native_wave import inspect_wave


def main():
    tree = MixTree()
    verified = {"music": [], "announcer": []}
    for kind, folder in (("music", ROOT / "assets/music/native"), ("announcer", ROOT / "assets/sfx/announcer")):
        report = json.loads((folder / "source_report.json").read_text(encoding="utf-8"))
        for key, record in report["tracks" if kind == "music" else "samples"].items():
            filename = record["sound"] + ".wav" if kind == "music" else key + ".wav"
            candidates = [(source, data) for source, data in tree.find_all(filename) if source == record["source"]]
            assert len(candidates) == 1, (filename, record["source"])
            original = candidates[0][1]
            exported = (folder / (key + ".wav")).read_bytes()
            assert original == exported, key
            info = inspect_wave(exported)
            for field in ("sha256", "frames", "channels", "rate"):
                assert info[field] == record[field], (key, field)
            verified[kind].append(key)
        if kind == "music":
            assert len(report["scenes"]["ra2.Battle"]) == 13
            assert "ra2_hm2" in report["scenes"]["ra2.Battle"]
            assert report["scenes"]["ra2.Menu"] == ["ra2_grinder"]
            assert report["scenes"]["ra2.Score"] == ["ra2_ra2-sco"]
        else:
            header = (ROOT / "src/game/eva_cues.h").read_text(encoding="utf-8")
            names = re.findall(r'X\(\w+, "(EVA_[^"]+)"\)', header)
            for name in names:
                cue = report["cues"][name]
                assert cue["Allied"].startswith("ceva") and cue["Soviet"].startswith("csof"), name
                if cue["Yuri"]:
                    assert cue["Yuri"].startswith("cyur"), name
    try:
        inspect_wave(bytes(1024))
        raise AssertionError("zero-filled placeholder passed native validation")
    except ValueError:
        pass
    print(f"NATIVE_ASSETS PASS music={len(verified['music'])} dialogue={len(verified['announcer'])} byte_exact=1 zero_placeholders_rejected=1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
