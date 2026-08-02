import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, d = t.find("mtnk.vxl")
print("len:", len(d))
print("id:", d[:16])
palcount, nsec, nsec2, bodysize = struct.unpack_from("<IIII", d, 16)
print(f"palcount={palcount} nsec={nsec} nsec2={nsec2} bodysize={bodysize}")
# candidate header ends
for hdr in (800, 802, 804, 806):
    hs = hdr + 34 * nsec
    tail = hs + bodysize
    end = tail + 92 * nsec
    print(f"  hdr_end={hdr}: sections_end={hs} tailer_end={end} vs len={len(d)} diff={len(d)-end}")
# dump first section header name at each candidate
for hdr in (800, 802, 804, 806):
    print(f"  name@{hdr}:", d[hdr:hdr+16])
