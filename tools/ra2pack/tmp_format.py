"""Ground-plane TMP decoding, independent of MIX discovery and export side effects.

Format cross-check: OpenRA/OpenRA.Mods.Cnc/SpriteLoaders/TmpTSLoader.cs.
Only the indexed ground image is decoded here, not extra cliff/depth images.
"""
import struct
from PIL import Image


def decode_diamond(data, offset, palette, width=60, height=30):
    if height < 2 or height % 2 or width != height * 2:
        raise ValueError("unsupported TMP diamond dimensions")
    count = width * height // 2
    if offset < 0 or offset + count > len(data):
        raise ValueError("truncated TMP ground pixels")
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    cursor = offset
    for y in range(height):
        row_width = 4 * min(y + 1, height - 1 - y)
        left = (width - row_width) // 2
        for x in range(row_width):
            index = data[cursor]
            cursor += 1
            if index:
                pixels[left + x, y] = (*palette[index], 255)
    if cursor != offset + count:
        raise ValueError("invalid TMP diamond pixel count")
    return image


def decode_tmp_frame(data, index, palette):
    if len(data) < 16:
        raise ValueError("truncated TMP header")
    columns, rows, width, height = struct.unpack_from("<4I", data)
    if height < 2 or height > 512 or height % 2 or width != height * 2:
        raise ValueError("unsupported TMP frame dimensions")
    count = columns * rows
    if count == 0 or count > 65536 or len(data) < 16 + count * 4 or not 0 <= index < count:
        raise ValueError("invalid TMP frame index table")
    offset = struct.unpack_from("<I", data, 16 + index * 4)[0]
    if not offset:
        return Image.new("RGBA", (width, height), (0, 0, 0, 0))
    if offset < 16 + count * 4:
        raise ValueError("TMP frame overlaps index table")
    return decode_diamond(data, offset + 52, palette, width, height)
