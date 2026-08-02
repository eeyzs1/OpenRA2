import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from Crypto.Cipher import Blowfish
from ra2lib import GAME_DIR, name_hash

p = os.path.join(GAME_DIR, "RA2.MIX")
full = open(p, "rb").read()
fsize = len(full)
print("file size:", fsize)
ks = full[4:84]
n = int("681994811107118991598552881669230523074742337494683459234572860554038768387821901289207730765589")
key = b""
for i in (0, 40):
    m = int.from_bytes(ks[i:i+40], "little")
    c = pow(m, 65537, n)
    key += c.to_bytes((c.bit_length()+7)//8 or 1, "little")
key = key[:56]
print("key:", key.hex())

cipher = Blowfish.new(key, Blowfish.MODE_ECB)
def dec_block(b8):
    b = b8[0:4][::-1]+b8[4:8][::-1]
    o = cipher.decrypt(b)
    return o[0:4][::-1]+o[4:8][::-1]

prev = bytes(8)
def dec_stream(data):
    out = bytearray(); pv = bytes(8)
    for i in range(0, len(data), 8):
        blk = data[i:i+8]
        dec = dec_block(blk)
        out += bytes(a^b for a,b in zip(dec, pv))
        pv = blk
    return bytes(out)

first = dec_block(full[84:92])
count, body = struct.unpack_from("<HI", first, 0)
print("count", count, "body", body)
idx_len = 6 + 12*count
padded = (idx_len + 7) & ~7
print("padded idx len", padded, "hdr total", 84+padded, "sum:", 84+padded+body, "+20 =", 84+padded+body+20)
rest = dec_stream(full[84:84+padded])
count, body = struct.unpack_from("<HI", rest, 0)
entries = []
ok = True
prev_off = 0
maxend = 0
ids_sorted = True
last_id = -1
for i in range(count):
    eid, off, size = struct.unpack_from("<III", rest, 6+i*12)
    entries.append((eid, off, size))
    if eid < last_id: ids_sorted = False
    last_id = eid
    if off < 0 or size < 0 or off+size > body: ok = False
    maxend = max(maxend, off+size)
print("ids sorted:", ids_sorted, "all within body:", ok, "maxend:", maxend, "body:", body)
print("first 5 entries:", [(hex(e),o,s) for e,o,s in entries[:5]])
# check known file
h = name_hash("local mix database.dat")
print("lmd hash %08x in index:" % h, h in {e[0] for e in entries})
h2 = name_hash("rules.ini")
print("rules.ini hash %08x:" % h2, h2 in {e[0] for e in entries})
