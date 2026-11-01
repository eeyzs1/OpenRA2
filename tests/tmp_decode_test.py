"""Validate TMP layout without needing installed game assets."""
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools/ra2pack"))
from tmp_format import decode_tmp_frame

palette = [(i, i, i) for i in range(256)]
# 8x4 diamond has rows 4,8,4,0. Deliberately distinct header bytes must not leak.
data = struct.pack("<5I", 1, 1, 8, 4, 20) + bytes([250]) * 52 + bytes(range(1,17))
image = decode_tmp_frame(data, 0, palette)
rows = [[image.getpixel((x,y))[0] for x in range(8)] for y in range(4)]
expected = [[0,0,1,2,3,4,0,0], list(range(5,13)), [0,0,13,14,15,16,0,0], [0]*8]
if rows != expected:
    raise AssertionError(f"TMP row/header mismatch: {rows}")
for invalid in [data[:-1], data[:18]]:
    try:
        decode_tmp_frame(invalid,0,palette)
    except ValueError:
        continue
    raise AssertionError("Truncated TMP accepted")
print("TMP DECODE PASS: indexed offset +52; diamond rows; exact pixels; truncation rejection")
