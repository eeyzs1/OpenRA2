import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import _derive_blowfish_key, _BfCbc, name_hash, GAME_DIR

p = os.path.join(GAME_DIR, "RA2.MIX")
d = open(p, "rb").read(200)
flags = struct.unpack_from("<I", d, 0)[0]
print("flags=%08x" % flags)
key = _derive_blowfish_key(d[4:84])
print("bf key len", len(key), key.hex())
bf = _BfCbc(key)
first = bf.decrypt_block(d[84:92])
print("first block:", first.hex(), struct.unpack_from("<HI", first, 0))
print("hash('local mix database.dat')=%08x" % name_hash("local mix database.dat"))
print("hash('rules.ini')=%08x" % name_hash("rules.ini"))
