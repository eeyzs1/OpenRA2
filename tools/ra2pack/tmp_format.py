"""TMP ground and full cliff-frame decoding, independent of MIX discovery.

Format cross-check: OpenRA/OpenRA.Mods.Cnc/SpriteLoaders/TmpTSLoader.cs.
The ground-only API stays available; the full-frame API adds authored extra
images while keeping depth bytes out of the visible artwork.
"""
import struct
from dataclasses import dataclass
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


@dataclass
class TmpFrame:
    image: Image.Image
    offset: tuple
    terrain_type: int
    ramp: int
    height: int


def decode_tmp_full_frame(data, index, palette):
    """Ground + extra cliff image at its authored offset; no rescaling/tinting.

    EA/XCC t_tmp_image_header stores extra and depth offsets relative to the
    frame header. Depth is not color data and must never become visible pixels.
    """
    ground = decode_tmp_frame(data, index, palette)
    columns, rows, width, height = struct.unpack_from("<4I", data)
    offset = struct.unpack_from("<I", data, 16 + index * 4)[0]
    if not offset:
        raise ValueError("native map references empty TMP subtile")
    if offset + 52 > len(data):
        raise ValueError("truncated TMP frame header")
    x, y, extra, depth, extra_depth, ex, ey, ew, eh, flags = struct.unpack_from("<iiIIIiiiiI", data, offset)
    z, terrain_type, ramp = struct.unpack_from("<BBB", data, offset + 40)
    if terrain_type > 15 or ramp > 20:
        raise ValueError("invalid TMP terrain metadata")
    if not flags & 1:
        return TmpFrame(ground, (0, 0), terrain_type, ramp, z)
    # x/y are the frame's position within the whole multi-cell template.
    ex -= x
    ey -= y
    if not 0 < ew <= 1024 or not 0 < eh <= 1024 or abs(ex) > 2048 or abs(ey) > 2048:
        raise ValueError("invalid TMP extra image bounds")
    if extra < 52 + width * height // 2 or offset + extra + ew * eh > len(data):
        raise ValueError("truncated TMP extra image")
    left, top = min(0, ex), min(0, ey)
    right, bottom = max(width, ex + ew), max(height, ey + eh)
    image = Image.new("RGBA", (right - left, bottom - top))
    image.paste(ground, (-left, -top))
    colors = [(*palette[i], 255) if i else (0, 0, 0, 0) for i in data[offset + extra:offset + extra + ew * eh]]
    extra_image = Image.new("RGBA", (ew, eh)); extra_image.putdata(colors)
    image.alpha_composite(extra_image, (ex - left, ey - top))
    return TmpFrame(image, (left, top), terrain_type, ramp, z)
