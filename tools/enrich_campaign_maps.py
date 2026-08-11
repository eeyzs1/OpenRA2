#!/usr/bin/env python3
"""Enrich all campaign hand maps with denser terrain, neutrals, and pickets."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def enrich(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    lines = [ln for ln in text.replace("\r\n","\n").split("\n") if ln.strip()!=""]
    # parse size
    m = re.search(r"^size\s+(\d+)\s+(\d+)", "\n".join(lines), re.M)
    if not m: return
    w = int(m.group(1))
    # already enriched marker
    if any("ENRICHED" in ln for ln in lines):
        return
    extra = [
        f"# ENRICHED layout pass",
        f"deco tree2 6 6 {max(8,w//4)} {max(8,w//4)} 8",
        f"deco tree3 {w//2} 6 {max(8,w//5)} {max(8,w//5)} 6",
        f"deco rock2 {w-24} {w//3} 14 12 5",
        f"blob ore {w//4} {w-28} 3",
        f"blob gems {w-30} {w//4} 2",
        f"bld -1 OilDerrick {w//3} {w-30}",
        f"bld -1 Hospital {w//2-4} {w-32}",
        f"bld -1 CivHouse {w//2} {w//2+8}",
        f"bld -1 CivHouse {w//2+6} {w//2+10}",
    ]
    # faction pickets near enemy if ConYard 1 exists
    if any(ln.startswith("bld 1 ConYard") for ln in lines):
        extra += [
            f"unit 1 Conscript {w-30} 28 guard",
            f"unit 1 Conscript {w-26} 32 guard",
            f"unit 1 AttackDog {w-28} 36 guard",
        ]
    if any(ln.startswith("bld 0 ") for ln in lines) or any(ln.startswith("unit 0 ") for ln in lines):
        extra += [
            f"unit 0 Engineer {12} {w-12}",
        ]
    body = "\n".join(lines + extra) + "\n"
    path.write_bytes(body.replace("\n","\r\n").encode("utf-8"))

def main():
    n=0
    for p in list((ROOT/"maps"/"official").glob("*.txt")) + list((ROOT/"maps"/"fusion").glob("*.txt")):
        enrich(p); n+=1
    print(f"Enriched {n} maps")

if __name__ == "__main__":
    main()
