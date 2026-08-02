import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, data = t.find("mtnk.vxl")
print("file len", len(data))
palcount, nsec, nsec2, bodysize = struct.unpack_from("<IIII", data, 16)
print("palcount", palcount, "nsec", nsec, "nsec2", nsec2, "bodysize", bodysize)
hdr_end = 16 + 16 + 768 + 2
print("name at hdr_end:", data[hdr_end:hdr_end + 16])
body_start = hdr_end + 28 * nsec
tailer_base = body_start + bodysize
sstart, send, sdata = struct.unpack_from("<III", data, tailer_base)
scale = struct.unpack_from("<f", data, tailer_base + 12)[0]
tf = struct.unpack_from("<12f", data, tailer_base + 16)
mins = struct.unpack_from("<3f", data, tailer_base + 64)
maxs = struct.unpack_from("<3f", data, tailer_base + 76)
sx, sy, sz = data[tailer_base + 88], data[tailer_base + 89], data[tailer_base + 90]
print("sstart", sstart, "send", send, "sdata", sdata, "scale", scale)
print("size", sx, sy, sz, "mins", mins, "maxs", maxs)
print("transform:", [round(v, 3) for v in tf])
body = data[body_start: body_start + bodysize]
ncol = sx * sy
print("ncol", ncol, "body len", len(body))
print("span_start table [0:8]:", struct.unpack_from("<8i", body, sstart))
print("span_end   table [0:8]:", struct.unpack_from("<8i", body, send))
# hypothesis A: span offsets relative to body
co = struct.unpack_from("<i", body, sstart)[0]
print("first span bytes @body+co:", body[co:co + 24].hex() if 0 <= co < len(body) else "OOB")
# hypothesis B: relative to sdata
print("first span bytes @body+sdata+co:", body[sdata + co:sdata + co + 24].hex() if 0 <= sdata + co < len(body) else "OOB")
# what does sdata point at?
print("bytes @sdata:", body[sdata:sdata + 16].hex() if 0 <= sdata < len(body) else "OOB")
# counts: how many columns have co != -1 under each hypothesis?
