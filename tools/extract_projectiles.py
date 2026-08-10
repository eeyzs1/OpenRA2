#!/usr/bin/env python3
"""Extract key weapon→projectile mappings from rulesmd dump for OpenRA2."""
from __future__ import annotations
import re
from pathlib import Path

RULESMD = Path(
    r"C:\Users\eeyzs1\.cursor\projects\e-AI-Generated-Projects-OpenRA2"
    r"\agent-tools\0ffe8ea3-db8d-40b6-b800-12625bb14054.txt"
)
OUT = Path(__file__).resolve().parents[1] / "assets" / "rules" / "projectiles.ini"

text = RULESMD.read_text(encoding="utf-8", errors="replace")
sections: dict[str, str] = {}
cur = None
buf: list[str] = []
for line in text.splitlines():
    m = re.match(r"^\[([^\]]+)\]\s*$", line)
    if m:
        if cur is not None:
            sections[cur] = "\n".join(buf)
        cur = m.group(1)
        buf = []
    else:
        buf.append(line)
if cur:
    sections[cur] = "\n".join(buf)


def get(sec: str, key: str, default: str | None = None) -> str | None:
    body = sections.get(sec, "")
    m = re.search(rf"^{re.escape(key)}=(.*)$", body, re.I | re.M)
    if not m:
        return default
    return m.group(1).strip().split(";")[0].strip()


def yn(v: str | None) -> bool:
    if v is None:
        return False
    return v.lower() in ("yes", "true", "1")


# Known projectile sections from earlier scan
PROJ_NAMES = [
    "Cannon", "Ballistic", "FlakTProj", "Lobbed", "Lobbed2", "GrandCannonBall",
    "AAHeatSeeker", "AAHeatSeeker2", "AAHeatSeeker3", "HeatSeeker",
    "AirToGroundMissile", "NavalToGroundSeeker", "DredMissile", "ChemMissile",
    "Torpedo", "ProtonTorpedo", "ProtonBlast", "DepthCharge", "NormalBomb",
    "BlimpBombP", "BlimpBombPE", "MedusaProjectile", "Invisible", "Invisible2",
    "Invisible3", "InvisibleLow", "InvisibleMedium", "InvisibleHigh", "InvisibleVertical",
    "Psychic", "PsychicControl", "Electricbounce", "LLine", "LLine2", "Null",
    "ClusterBits", "DogShard", "JUMP", "DOGJUMP", "ADOGJUMP", "SQDJUMP",
]

# OpenRA2 projSprite bridge → primary rulesmd projectile + typical weapon Speed
# (Weapon Speed in leptons/frame; Arcing ignores and uses 50)
BRIDGE = {
    "shell": ("Cannon", 40),          # tank cannon family; Arcing
    "bullet": ("Invisible", 100),     # infantry bullets often inviso / very fast
    "flak": ("AAHeatSeeker", 60),     # flak missiles / seekers; ROT
    "missile": ("HeatSeeker", 40),    # ground missiles
    "naval": ("NavalToGroundSeeker", 40),
    "torpedo": ("Torpedo", 30),
    "rad": ("Invisible", 100),
    "psi": ("Psychic", 0),            # inviso
    "tesla": ("Electricbounce", 0),
    "prism": ("LLine", 0),
    "chrono": ("Invisible", 0),
}

weapons_of_interest = [
    "120mm", "105mm", "90mm", "M60", "AssaultCannon", "Howitzer",
    "FlakWeapon", "FlakTrackGun", "Patriot", "RedEye2", "SAMissile",
    "Maverick", "Hellfire", "ATGMissile", "AGMissile", "V3Rocket",
    "DredRocket", "TorpedoWeapon", "Subtorpedo", "ASW", "Sonic",
    "BlimpBomb", "HornetBomb", "Bomb", "PrismShot", "PsychicJab",
    "RadBeam", "Cleg", "GrandCannonWeapon", "20mmRapid", "RPJ",
    "BoomerMissile", "BoomerTorpedo", "Medusa", "CRGMissile",
]

print("=== weapons ===")
for w in weapons_of_interest:
    if w not in sections:
        print(f"MISS {w}")
        continue
    print(
        f"{w}: Dmg={get(w,'Damage')} ROF={get(w,'ROF')} Range={get(w,'Range')} "
        f"Speed={get(w,'Speed')} Proj={get(w,'Projectile')} WH={get(w,'Warhead')}"
    )

# Emit projectiles.ini for OpenRA2 (subset + bridge defaults)
lines = [
    "; Auto-derived subset of YR rulesmd projectiles for OpenRA2.",
    "; SpeedLeptons = weapon-applied leptons/frame when not Arcing; Arcing forces 50.",
    "; OpenRA2 tick (30fps): cells/tick = leptons/512  (RA2 15fps × leptons/256 / 2).",
    "; ROT: Westwood projectile turn rate; 0 = non-homing.",
    "",
]

# Build full projectile entries from rulesmd
for name in sorted(set(PROJ_NAMES) | {BRIDGE[k][0] for k in BRIDGE}):
    if name not in sections and name not in ("FlakTProj",):
        # FlakTProj might exist
        pass
    body = sections.get(name)
    if body is None:
        continue
    arcing = yn(get(name, "Arcing"))
    inviso = yn(get(name, "Inviso"))
    vertical = yn(get(name, "Vertical"))
    inaccurate = yn(get(name, "Inaccurate"))
    rot = int(float(get(name, "ROT", "0") or "0"))
    aa = yn(get(name, "AA"))
    ag = get(name, "AG")
    ag_b = True if ag is None else yn(ag)
    image = get(name, "Image", "none") or "none"
    proximity = yn(get(name, "Proximity"))
    lines.append(f"[Projectile.{name}]")
    lines.append(f"Image={image}")
    lines.append(f"Arcing={'yes' if arcing else 'no'}")
    lines.append(f"Inviso={'yes' if inviso else 'no'}")
    lines.append(f"Vertical={'yes' if vertical else 'no'}")
    lines.append(f"Inaccurate={'yes' if inaccurate else 'no'}")
    lines.append(f"ROT={rot}")
    lines.append(f"AA={'yes' if aa else 'no'}")
    lines.append(f"AG={'yes' if ag_b else 'no'}")
    lines.append(f"Proximity={'yes' if proximity else 'no'}")
    # Default leptons for mover when weapon doesn't override:
    # Arcing → 50; seekers → typical weapon speeds from table above
    default_spd = 50 if arcing else (100 if inviso else 40)
    if name.startswith("AAHeat"):
        default_spd = 60
    if name == "Torpedo":
        default_spd = 30
    if name == "HeatSeeker":
        default_spd = 40
    if name == "DredMissile":
        default_spd = 30
    lines.append(f"SpeedLeptons={default_spd}")
    lines.append("")

# Bridge aliases: OpenRA2 Weapon.Proj string → Projectile id
lines.append("; --- OpenRA2 Weapon.Proj bridge ---")
for sprite, (proj, spd) in BRIDGE.items():
    lines.append(f"[ProjectileBridge.{sprite}]")
    lines.append(f"Projectile={proj}")
    lines.append(f"SpeedLeptons={spd}")
    lines.append("")

OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
print(f"wrote {OUT} ({len(lines)} lines)")
