# 提取 art.ini 中的动画序列定义（权威帧映射来源）
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

T = MixTree()
mix, d = T.find("art.ini")
if not d:
    print("art.ini MISS"); raise SystemExit
txt = d.decode("latin-1")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "art.ini"), "w", encoding="latin-1").write(txt)
print("art.ini saved,", len(txt), "chars")

# 打印所有 Sequence 段
secs = {}
cur = None
for line in txt.splitlines():
    line = line.strip()
    if line.startswith("[") and line.endswith("]"):
        cur = line[1:-1]
        if "Sequence" in cur or "sequence" in cur:
            secs[cur] = []
    elif cur and "=" in line and cur in secs:
        secs[cur].append(line)
for name, lines in secs.items():
    print(f"\n[{name}]")
    for l in lines:
        print(" ", l)
