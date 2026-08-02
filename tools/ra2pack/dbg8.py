import sys, os, struct, base64
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import GAME_DIR, name_hash
from Crypto.Cipher import Blowfish

_PUBKEY_B64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"

def derive_key(key_source: bytes) -> bytes:
    blob = base64.b64decode(_PUBKEY_B64)
    print("decoded pubkey blob:", blob.hex())
    assert blob[0] == 2, "ASN.1 INTEGER tag expected"
    i = 1
    if blob[i] & 0x80:
        nlen = blob[i] & 0x7F
        keylen = int.from_bytes(blob[i+1:i+1+nlen], 'big')
        i += 1 + nlen
    else:
        keylen = blob[i]
        i += 1
    n = int.from_bytes(blob[i:i+keylen], 'big')
    print("modulus bitlen:", n.bit_length(), "keylen bytes:", keylen)
    pubkey_len = n.bit_length() - 1
    a = (pubkey_len - 1) // 8
    print("pubkey.len =", pubkey_len, " a =", a, " block in =", a+1, " block out =", a)
    out = b""
    pos = 0
    while a + 1 <= len(key_source) - pos:
        m = int.from_bytes(key_source[pos:pos+a+1], 'little')
        c = pow(m, 0x10001, n)
        out += (c % (1 << (8*a))).to_bytes(a, 'little')
        pos += a + 1
    return out[:56]

p = os.path.join(GAME_DIR, "RA2.MIX")
full = open(p, "rb").read()
fsize = len(full)
print("file size:", fsize)
flags = struct.unpack_from("<I", full, 0)[0]
print("flags=%08x" % flags)
ks = full[4:84]
key = derive_key(ks)
print("blowfish key:", key.hex())

cipher = Blowfish.new(key, Blowfish.MODE_ECB)
first = cipher.decrypt(full[84:92])
count, body = struct.unpack_from("<HI", first, 0)
print("count", count, "body", body)
if 0 < count < 60000 and 0 < body < fsize:
    idx_len = 6 + 12*count
    padded = (idx_len + 7) & ~7
    print("padded idx len", padded, " 84+padded+body =", 84+padded+body, " +20 =", 84+padded+body+20, " fsize =", fsize)
    out = cipher.decrypt(full[84:84+padded])
    count, body = struct.unpack_from("<HI", out, 0)
    ok = True
    ids = set()
    for i in range(count):
        eid, off, size = struct.unpack_from("<III", out, 6+i*12)
        ids.add(eid)
        if off + size > body:
            ok = False
    print("entries within body:", ok)
    print("lmd in index:", name_hash("local mix database.dat") in ids)
    print("rules.ini in index:", name_hash("rules.ini") in ids)
