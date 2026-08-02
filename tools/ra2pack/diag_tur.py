# 列出 MIX 中全部 *tur.vxl 炮塔文件
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree
T = MixTree()
names = ["gtnktur","htnktur","fvtur","sreftur","ttnktur","rtnktur","htktur","apoctur",
         "mtnktur","ltnktur","tnkdtur","v3tur","desttur","subtur","drontur","truckatur",
         "cmintur","harvtur","zep","beagtur","shadtur"]
for n in names:
    p = T.find(n + ".vxl")[1]
    h = T.find(n.split("tur")[0] + ".hva")[1]
    print(f"{n:12s} vxl={'Y' if p else '-'}  主体hva={'Y' if h else '-'}")
