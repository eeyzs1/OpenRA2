#!/usr/bin/env python3
"""Try multiple mirrors to fetch a YR-capable RA2 pack containing RA2MD.MIX."""
import os, sys, urllib.request, ssl

OUT = os.path.join(os.path.dirname(__file__), "dl", "RedAlert2.7z")
os.makedirs(os.path.dirname(OUT), exist_ok=True)

URLS = [
    "https://ia801800.us.archive.org/3/items/red-alert-2_202103/Red%20Alert%202.7z",
    "https://ia601800.us.archive.org/3/items/red-alert-2_202103/Red%20Alert%202.7z",
    "https://dn720200.ca.archive.org/0/items/red-alert-2_202103/Red%20Alert%202.7z",
    "https://archive.org/download/red-alert-2_202103/Red%20Alert%202.7z",
]

def try_head(url, timeout=20):
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "Mozilla/5.0 OpenRA2-asset-fetch"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return int(r.headers.get("Content-Length") or 0), r.headers.get("Content-Type")

def download(url, path, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 OpenRA2-asset-fetch"})
    with urllib.request.urlopen(req, timeout=timeout) as r, open(path, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        n = 0
        while True:
            chunk = r.read(1024 * 256)
            if not chunk:
                break
            f.write(chunk)
            n += len(chunk)
            if total:
                pct = 100.0 * n / total
                print(f"\r  {n}/{total} ({pct:.1f}%)", end="", flush=True)
        print()
        return n

for url in URLS:
    print("TRY", url)
    try:
        clen, ctype = try_head(url)
        print("  head ok", clen, ctype)
    except Exception as e:
        print("  head fail", type(e).__name__, e)
        # still try GET for first MB
    try:
        partial = OUT + ".part"
        n = download(url, partial)
        if n > 1_000_000:
            os.replace(partial, OUT)
            print("OK saved", OUT, "bytes", n)
            sys.exit(0)
        print("  too small", n)
    except Exception as e:
        print("  get fail", type(e).__name__, e)

print("ALL MIRRORS FAILED")
sys.exit(1)
