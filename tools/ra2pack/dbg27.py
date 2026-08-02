import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
for base in ["mtnk", "gtnk", "4tnk", "3tnk", "htnk", "mcv", "smcv", "rtnk"]:
    _, d = t.find(base + ".vxl")
    if not d:
        print(base, "missing"); continue
    palcount, nsec, nsec2, bodysize = struct.unpack_from("<IIII", d, 16)
    pred28 = 802 + 28 * nsec + bodysize + 92 * nsec
    pred34 = 802 + 34 * nsec + bodysize + 92 * nsec
    names = []
    for i in range(nsec):
        names.append(d[802 + i * 28: 802 + i * 28 + 16].split(b"\x00")[0].decode("latin-1", "replace"))
    print(f"{base:6s} len={len(d):7d} nsec={nsec} body={bodysize:6d} pred28_diff={len(d)-pred28} pred34_diff={len(d)-pred34} names={names}")

# mtnk tailer (header28 layout)
_, d = t.find("mtnk.vxl")
palcount, nsec, nsec2, bodysize = struct.unpack_from("<IIII", d, 16)
tbase = 802 + 28 * nsec + bodysize
print("\nmtnk tailer @", tbase)
sstart, send, sdata = struct.unpack_from("<III", d, tbase)
print("sstart,send,sdata:", sstart, send, sdata)
print("scale:", struct.unpack_from("<f", d, tbase + 12)[0])
print("mins:", struct.unpack_from("<3f", d, tbase + 64))
print("maxs:", struct.unpack_from("<3f", d, tbase + 76))
print("sizes:", d[tbase+88], d[tbase+89], d[tbase+90], "normalmode:", d[tbase+91])
