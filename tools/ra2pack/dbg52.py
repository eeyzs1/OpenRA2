import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, _a = t.find("art.ini")
AT = _a.decode("latin-1", "replace")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        m = re.match(r"\[(.+?)\]", line)
        if m:
            cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs
A = parse_ini(AT)
for sec in ["GACNST", "GAPOWR", "NAPOWR", "GAPILE"]:
    print(sec, "->", {k: v for k, v in A.get(sec, {}).items() if "ameo" in k or k == "Image"})

# cameo 文件实际探测
def has(n): return t.find(n)[1] is not None
for c in ["cnsticon.shp", "gacnsticon.shp", "ngcnstuc.shp", "cgcnstuc.shp", "powricon.shp", "gapowricon.shp"]:
    print(c, has(c))
