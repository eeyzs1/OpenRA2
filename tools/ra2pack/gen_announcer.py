"""Export real EVA/Sofia/Yuri dialogue; unavailable recordings stay explicit."""
import argparse
import json
from pathlib import Path
from ra2lib import MixTree
from gen_voices import parse_ini
from native_wave import find_wave

ROOT = Path(__file__).resolve().parents[2]


def export_announcer(tree, output):
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    definitions, records = {}, {}
    for filename in ("eva.ini", "evamd.ini"):
        source, data = tree.find(filename)
        if not data:
            continue
        ini = parse_ini(data)
        for name in ini.get("dialoglist", {}).values():
            if name and name.lower() in ini:
                definitions.setdefault(name, {}).update(ini[name.lower()])
    report = {"cues": {}, "samples": {}, "unavailable": []}
    lines = ["; Original EVA dialogue; empty speaker fields mean native source unavailable.", "[Meta]", "Version=1", ""]
    ranks = {"LOW": 0, "NORMAL": 1, "IMPORTANT": 2, "CRITICAL": 3}
    for name, values in definitions.items():
        cue = {"Priority": ranks[values.get("priority", "NORMAL").upper()],
               "Queued": values.get("type", "").upper() == "QUEUE"}
        for side, field in (("Allied", "allied"), ("Soviet", "russian"), ("Yuri", "yuri")):
            sample = values.get(field, "").lower()
            cue[side] = ""
            if not sample:
                continue
            source, data, info = find_wave(tree, sample + ".wav", "langmd" if side == "Yuri" else "language.mix")
            if not data:
                report["unavailable"].append({"cue": name, "side": side, "sample": sample, **info})
                continue
            if sample not in records:
                destination = output / (sample + ".wav"); staging = destination.with_suffix(".wav.tmp")
                staging.write_bytes(data); staging.replace(destination)
                records[sample] = {"source": source, **info}
            cue[side] = sample
        # China is an existing Soviet-derived expansion, not a native EVA voice.
        cue["China"] = cue["Soviet"]
        report["cues"][name] = cue
        lines += [f"[Cue.{name}]", f"Priority={cue['Priority']}", f"Queued={int(cue['Queued'])}"]
        lines += [f"{side}={cue[side]}" for side in ("Allied", "Soviet", "China", "Yuri")]
        lines += [""]
    for required in ("EVA_LowPower", "EVA_UnitReady", "EVA_ConstructionComplete", "EVA_UnitLost", "EVA_YouAreVictorious", "EVA_YouHaveLost"):
        if required not in report["cues"] or not all(report["cues"][required][side] for side in ("Allied", "Soviet")):
            raise ValueError("required RA2 dialogue is unavailable: " + required)
    report["samples"] = records
    temporary = output / "manifest.ini.tmp"; temporary.write_text("\n".join(lines), encoding="utf-8"); temporary.replace(output / "manifest.ini")
    (output / "source_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--output", type=Path, default=ROOT / "assets/sfx/announcer")
    args = parser.parse_args(); result = export_announcer(MixTree(), args.output)
    print(f"ANNOUNCER EXPORTED cues={len(result['cues'])} clips={len(result['samples'])} unavailable={len(result['unavailable'])}")
