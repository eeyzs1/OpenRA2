"""Export native theme files and scene playlists, never generated substitutes."""
import argparse
import json
from pathlib import Path
from ra2lib import MixTree
from gen_voices import parse_ini
from native_wave import find_wave

ROOT = Path(__file__).resolve().parents[2]


def export_music(tree, output):
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    report = {"tracks": {}, "scenes": {}, "unavailable": []}
    for edition, filename in (("ra2", "theme.ini"), ("yr", "thememd.ini")):
        source, data = tree.find(filename)
        if not data:
            continue
        ini = parse_ini(data); battle, defined = [], {}
        for section in ini.get("themes", {}).values():
            fields = ini.get(section.lower(), {})
            sound = fields.get("sound")
            if not section or not sound:
                continue
            mix, wave, info = find_wave(tree, sound + ".wav", "thememd.mix" if edition == "yr" else "theme.mix")
            if not wave:
                report["unavailable"].append({"edition": edition, "theme": section, **info})
                continue
            key = edition + "_" + sound.lower()
            if key not in report["tracks"]:
                destination = output / (key + ".wav"); staging = destination.with_suffix(".wav.tmp")
                staging.write_bytes(wave); staging.replace(destination)
                report["tracks"][key] = {"source": mix, "theme": section, "sound": sound, **info}
            defined[section.lower()] = key
            if fields.get("normal", "yes").lower() != "no":
                battle.append(key)
        for scene, section in (("Menu", "intro"), ("Loading", "loading"), ("Score", "score"), ("Credits", "credits")):
            report["scenes"][edition + "." + scene] = [defined[section]] if section in defined else []
        report["scenes"][edition + ".Battle"] = list(dict.fromkeys(battle))
    if len(report["scenes"].get("ra2.Battle", [])) < 13 or not report["scenes"].get("ra2.Menu") or not report["scenes"].get("ra2.Score"):
        raise ValueError("authentic RA2 soundtrack is incomplete; keep the previous manifest")
    lines = ["; Native soundtrack copied byte-for-byte from MIX; loose generated loops are not scanned.", "[Meta]", "Version=1", ""]
    for scene, tracks in report["scenes"].items():
        lines += [f"[{scene}]"] + [f"Track={key}.wav" for key in tracks] + [""]
    temporary = output / "manifest.ini.tmp"; temporary.write_text("\n".join(lines), encoding="utf-8"); temporary.replace(output / "manifest.ini")
    (output / "source_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--output", type=Path, default=ROOT / "assets/music/native")
    args = parser.parse_args(); result = export_music(MixTree(), args.output)
    print(f"MUSIC EXPORTED tracks={len(result['tracks'])} ra2_battle={len(result['scenes']['ra2.Battle'])} unavailable={len(result['unavailable'])}")
