import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import GAME_DIR

p = os.path.join(GAME_DIR, "RA2.MIX")
d = open(p, "rb").read(4096)
ks = d[4:84]
print("key_source block1:", ks[:40].hex())
print("key_source block2:", ks[40:].hex())
# try: pow with e=65537 and the rust-crate modulus, but interpret decrypted as is and look at sizes
n = int("681994811107118991598552881669230523074742337494683459234572860554038768387821901289207730765589")
print("modulus bitlen:", n.bit_length())
for i in (0, 40):
    m = int.from_bytes(ks[i:i+40], "little")
    c = pow(m, 65537, n)
    print("block", i//40, "m bitlen:", m.bit_length(), "c bitlen:", c.bit_length(), "c bytes le:", c.to_bytes((c.bit_length()+7)//8, "little").hex())
    m2 = int.from_bytes(ks[i:i+40], "big")
    c2 = pow(m2, 65537, n)
    print("   big-end c bitlen:", c2.bit_length(), c2.to_bytes((c2.bit_length()+7)//8, "big").hex())
# also dump first encrypted index block
print("enc idx blk0:", d[84:92].hex())
print("enc idx blk1:", d[92:100].hex())
