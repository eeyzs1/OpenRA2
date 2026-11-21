"""Map-codec boundary tests; synthetic data needs no installed game assets."""
import base64
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/ra2pack"))
from map_format import decode_lcw, decode_pack, parse_map, read_ini
from tmp_format import decode_tmp_full_frame


def reject(function, *args):
    try:
        function(*args)
    except (ValueError, KeyError):
        return
    raise AssertionError(f"malformed data accepted: {function.__name__}")


# Literal, relative overlap, absolute overlap, long copy, fill and terminator.
encoded = b"\x83ABC\x00\x03\xC0\x00\x00\xFF\x03\x00\x01\x00\xFE\x04\x00!\x80"
assert decode_lcw(encoded, 16) == b"ABCABCABCBCA!!!!"
for corrupt in [b"", encoded[:-1], encoded + b"\0", b"\x00\x01\x80", b"\xFE\xFF\xFF!\x80"]:
    reject(decode_lcw, corrupt, 16)
reject(decode_lcw, encoded, 15)
reject(decode_lcw, encoded, 17)

def packed(blocks):
    return {"1": base64.b64encode(b"".join(struct.pack("<HH", len(b), n) + b for b, n in blocks)).decode()}

assert decode_pack(packed([(b"\x81A\x80", 1), (b"\x81B\x80", 1)]), "lcw", 2) == b"AB"
reject(decode_pack, {"2": "AAAA"}, "lcw", 1)
reject(decode_pack, {"1": "!invalid!"}, "lcw", 1)
reject(decode_pack, packed([(b"\x81A\x80", 2)]), "lcw", 2)
reject(decode_pack, packed([(b"\x81A\x80", 1)]), "lcw", 2)

assert read_ini(b"garbage\n[X]\n// prose\nKeY=One;comment\nKEY=Two\n") == {"x": {"key": "Two"}}

# Original TMP has a separate 4-bit terrain code, NOT the LandType enum.
header = bytearray(52)
struct.pack_into("<iiIIIiiiiI", header, 0, 4, 2, 84, 68, 88, 2, 1, 2, 2, 1)
header[40:43] = bytes([4, 15, 3])
data = struct.pack("<5I", 1, 1, 8, 4, 20) + header + bytes(range(1,17)) + bytes([250]) * 16 + bytes([50,0,51,52])
palette = [(i,i,i) for i in range(256)]
frame = decode_tmp_full_frame(data, 0, palette)
assert frame.offset == (-2, -1) and frame.image.size == (10, 5)
assert (frame.terrain_type, frame.ramp, frame.height) == (15,3,4)
assert frame.image.getpixel((0,0)) == (50,50,50,255)
assert frame.image.getpixel((1,0))[3] == 0
assert 250 not in [frame.image.getpixel((x,y))[0] for y in range(5) for x in range(10)]
reject(decode_tmp_full_frame, data[:-1], 0, palette)

print("NATIVE MAP FORMAT PASS: block framing; LCW command modes/bounds; original TMP metadata, cliff offsets and depth separation")
