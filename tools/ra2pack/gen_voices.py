"""Export native RA2/YR unit acknowledgements from paired MIX audio archives.

No synthesized replacements. RA2 units prefer RA2 rules/sounds; YR additions
use rulesmd/soundmd. IDX and BAG always come from the same containing MIX.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import struct
import wave

from ra2lib import MixTree
from gen_audio import decode_ima_blocks, decode_pcm16, FLAG_ADPCM, FLAG_PCM, FLAG_STEREO

ROOT = Path(__file__).resolve().parents[2]
ACTIONS = {"Select": "VoiceSelect", "Move": "VoiceMove", "Attack": "VoiceAttack",
           "Harvest": "VoiceHarvest", "Deploy": "VoiceDeploy", "Special": "VoiceSpecialAttack"}
SIDES = {"mcv.allies": ["AMCV"], "mcv.soviet": ["SMCV"], "mcv.yuri": ["PCV"],
         "mcv.china": ["SMCV"],
         "attackdog.allies": ["ADOG"], "attackdog.soviet": ["DOG"],
         "amphtransport.allies": ["LCRF"], "amphtransport.soviet": ["SAPC"],
         "amphtransport.yuri": ["YHVR"]}


def parse_ini(data):
    result, current = {}, None
    for line in data.decode("latin-1").splitlines():
        line = line.split(";", 1)[0].strip()
        if line.startswith("[") and "]" in line:
            current = result.setdefault(line[1:line.index("]")].casefold(), {})
        elif current is not None and "=" in line:
            key, value = line.split("=", 1)
            current[key.strip().casefold()] = value.strip()
    return result


def unit_mapping():
    # Share the existing asset mapping without importing its image generator.
    tree = ast.parse((Path(__file__).with_name("gen_assets.py")).read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(x, ast.Name) and x.id == "UNITS" for x in node.targets):
            return ast.literal_eval(node.value) | SIDES
    raise ValueError("gen_assets.py has no literal UNITS mapping")


def audio_entries(index, bag):
    if len(index) < 12 or index[:4] != b"GABA":
        raise ValueError("unrecognized audio.idx header")
    version, count = struct.unpack_from("<II", index, 4)
    if version != 2 or len(index) != 12 + 36 * count:
        raise ValueError("invalid audio.idx count/version")
    result = {}
    for offset in range(12, len(index), 36):
        name = index[offset:offset + 16].split(b"\0", 1)[0].decode("ascii").lower()
        entry = struct.unpack_from("<IIIII", index, offset + 16)
        start, length, rate, flags, chunk = entry
        if not name or name in result or length == 0 or start + length > len(bag) or not 4000 <= rate <= 96000:
            raise ValueError(f"invalid audio entry {name!r}")
        result[name] = entry
    return result


def decode_sample(raw, rate, flags, chunk):
    if flags & FLAG_STEREO:
        raise ValueError("stereo unit speech requires a native stereo decoder; do not decode it as mono")
    if flags & FLAG_ADPCM:
        if chunk < 4 or chunk > 65536:
            raise ValueError("invalid IMA block size")
        samples = decode_ima_blocks(raw, block=chunk)
    elif flags & FLAG_PCM:
        if len(raw) % 2:
            raise ValueError("unaligned PCM16 speech")
        samples = decode_pcm16(raw)
    else:
        raise ValueError(f"unsupported native audio flags {flags}")
    if not samples or len(samples) > rate * 30:
        raise ValueError("invalid speech duration")
    # Preserve the decoded waveform, sample rate, duration and pitch.
    return struct.pack(f"<{len(samples)}h", *samples)


def export_voices(tree, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    editions, archive_bags = {}, dict(tree.find_all("audio.bag"))
    report = {"version": 1, "archives": [], "profiles": {}, "groups": {}, "samples": {}}
    for edition, rules_file, sounds_file in [("ra2", "rules.ini", "sound.ini"), ("yr", "rulesmd.ini", "soundmd.ini")]:
        rules_source, rules = tree.find(rules_file)
        sound_source, sounds = tree.find(sounds_file)
        if rules and sounds:
            editions[edition] = (parse_ini(rules), parse_ini(sounds))
            report.setdefault("definitions", {})[edition] = {"rules": rules_source, "sounds": sound_source,
                "rules_sha256": hashlib.sha256(rules).hexdigest(), "sound_sha256": hashlib.sha256(sounds).hexdigest()}
    archives = []
    for source, index in tree.find_all("audio.idx"):
        if source not in archive_bags:
            raise ValueError(f"audio.idx has no audio.bag in {source}")
        bag = archive_bags[source]
        edition = "yr" if "audiomd.mix" in source.casefold() else "ra2"
        entries = audio_entries(index, bag)
        archives.append((edition, source, entries, bag))
        report["archives"].append({"edition": edition, "source": source, "entries": len(entries),
            "idx_sha256": hashlib.sha256(index).hexdigest(), "bag_sha256": hashlib.sha256(bag).hexdigest()})
    if "ra2" not in editions or not archives:
        raise ValueError("native RA2 rules/sounds and paired audio.idx/bag are required")

    groups, profiles, exported = {}, {}, {}
    priority_names = {"lowest": 0, "low": 1, "normal": 2, "high": 3, "critical": 4}

    def export_sample(edition, name):
        candidates = sorted(archives, key=lambda item: (item[0] != edition, item[1]))
        for family, source, entries, bag in candidates:
            if name not in entries:
                continue
            key = f"{family}_{name}"
            if key in exported:
                return key
            start, size, rate, flags, chunk = entries[name]
            native = bag[start:start + size]
            pcm = decode_sample(native, rate, flags, chunk)
            destination = output / f"{key}.wav"
            staging = destination.with_suffix(".wav.tmp")
            with wave.open(str(staging), "wb") as writer:
                writer.setnchannels(1)
                writer.setsampwidth(2)
                writer.setframerate(rate)
                writer.writeframes(pcm)
            staging.replace(destination)
            exported[key] = {"source": source, "sample": name, "rate": rate, "flags": flags, "chunk": chunk,
                "frames": len(pcm) // 2, "native_sha256": hashlib.sha256(native).hexdigest(),
                "pcm_sha256": hashlib.sha256(pcm).hexdigest()}
            return key
        raise ValueError(f"native speech sample missing: {edition}:{name}")

    def export_group(edition, name):
        key = f"{edition}_{name.casefold()}"
        if key in groups:
            return key
        sound = editions[edition][1].get(name.casefold())
        if not sound:
            raise ValueError(f"native sound group missing: {edition}:{name}")
        names = [x.lstrip("$") for x in re.split(r"[\s,]+", sound.get("sounds", "")) if x]
        if not names or len(names) > 64 or any(not re.fullmatch(r"[a-zA-Z0-9_]+", x) for x in names):
            raise ValueError(f"invalid native group samples: {edition}:{name}")
        volume = float(sound.get("volume", "100"))
        if not 0 <= volume <= 100:
            raise ValueError(f"invalid native speech volume: {name}")
        priority = sound.get("priority", "normal").lower()
        if priority not in priority_names:
            raise ValueError(f"unknown native priority: {name}:{priority}")
        groups[key] = {"Samples": list(dict.fromkeys(export_sample(edition, x.lower()) for x in names)),
            "Volume": volume, "Priority": priority_names[priority]}
        report["groups"][key] = {"native_group": name, "edition": edition, "control": sound.get("control", ""), **groups[key]}
        return key

    for engine, candidates in unit_mapping().items():
        found = None
        for edition in ("ra2", "yr"):
            if edition not in editions:
                continue
            for native in candidates:
                values = editions[edition][0].get(native.casefold())
                if values is not None:
                    found = edition, native, values
                    break
            if found:
                break
        profile = {}
        if found:
            edition, native, values = found
            for action, field in ACTIONS.items():
                names = [x.strip() for x in values.get(field.casefold(), "").split(",") if x.strip()]
                profile[action] = list(dict.fromkeys(export_group(edition, x) for x in names))
            report["profiles"][engine] = {"edition": edition, "native": native, "actions": profile}
        else:
            report["profiles"][engine] = {"native": None, "reason": "No native unit definition; intentionally unvoiced", "actions": {}}
        profiles[engine] = profile

    lines = ["; Native unit speech. Generated by tools/ra2pack/gen_voices.py; never synthesize substitutes.", "[Meta]", "Version=1", ""]
    for engine, profile in sorted(profiles.items()):
        lines.append(f"[Unit.{engine}]")
        for action in ACTIONS:
            lines.append(f"{action}={','.join(profile.get(action, []))}")
        lines.append("")
    for key, group in sorted(groups.items()):
        lines.extend([f"[Group.{key}]", f"Samples={','.join(group['Samples'])}", f"Volume={group['Volume']:g}",
                      f"Priority={group['Priority']}", ""])
    temporary = output / "manifest.ini.tmp"
    temporary.write_text("\n".join(lines), encoding="utf-8")
    temporary.replace(output / "manifest.ini")
    report["samples"] = exported
    (output / "source_report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "assets/sfx/voices")
    args = parser.parse_args()
    report = export_voices(MixTree(), args.output)
    silent = [name for name, profile in report["profiles"].items() if not any(profile["actions"].values())]
    print(f"VOICES EXPORTED profiles={len(report['profiles'])} groups={len(report['groups'])} clips={len(report['samples'])}")
    print("VOICES INTENTIONALLY SILENT " + ",".join(silent))


if __name__ == "__main__":
    main()
