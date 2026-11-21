"""Shared original voxel actors for native maps and map-independent rules."""
import hashlib
import math
from pathlib import Path

from PIL import Image
from ra2lib import Vxl, Hva, vxl_project, facing_world_phi, render_pts


def missile_poses(get, identity, image, palette, target, scale=64 / 60):
    """Keep the original voxel pivot fixed across pitch and look directions."""
    voxel = get(image + '.vxl')[1];transform = get(image + '.hva')[1]
    if not voxel or not transform:
        raise FileNotFoundError('original missile voxel/HVA missing: ' + image)
    v, h = Vxl(voxel), Hva(transform)
    poses = []
    for pose in range(17):
        for direction in range(8):
            points, _ = vxl_project(v, h, facing_world_phi(direction), pitch=(pose - 8) * math.pi / 16)
            if not points:
                raise ValueError('empty original missile pose: ' + identity)
            poses.append(points)
    extent_x = max(abs(point[0]) for points in poses for point in points) + 1.3
    extent_y = max(abs(point[1]) for points in poses for point in points) + 1.3
    width = max(72, 2 * math.ceil(extent_x * scale + 4))
    height = max(72, 2 * math.ceil(extent_y * scale + 8))
    origin_x, origin_y = width // 2, height // 2 + 4
    target = Path(target);target.mkdir(parents=True, exist_ok=True)
    for frame, points in enumerate(poses):
        render_pts(points, palette, scale, origin_x, origin_y, width, height, supersample=2).save(target / f'{identity.lower()}_d{frame}.png')
    return {'Remappable': '0', 'OriginX': str(origin_x), 'OriginY': str(origin_y), 'ArtFrames': str(len(poses))}


def launcher_art(get, identity, image, palette, target, alternate):
    """Render ready/empty source models with a common layout and house mask."""
    import gen_assets as raster
    old_get, old_palette = raster.get, raster.PAL_U
    target = Path(target);target.mkdir(parents=True, exist_ok=True)
    try:
        raster.get = lambda name: get(name)[1]
        ready = raster._project_stem_dirs(image)
        empty = raster._project_stem_dirs(image + 'wo') if alternate else None
        if not ready or (alternate and not empty):
            raise FileNotFoundError('original launcher voxel missing: ' + image)
        all_points = raster._merge_per(ready, empty) if alternate else ready
        _, layout = raster._rasterize_per(all_points, (72, 72), False, anchor_per=ready)
        mask_palette = [color if 16 <= i <= 31 else (0, 0, 0) for i, color in enumerate(palette)]
        for offset, variant in enumerate([ready, empty] if alternate else [ready]):
            for direction, points in enumerate(variant):
                args = (layout['scale'], *layout['orgs'][direction], layout['w'], layout['ch'])
                body = render_pts(points[0], palette, *args, supersample=2)
                marker = render_pts(points[0], mask_palette, *args, supersample=2)
                frame = direction + offset * 8
                body.save(target / f'{identity.lower()}_d{frame}.png')
                mask = Image.new('L', body.size)
                for y in range(body.height):
                    for x in range(body.width):
                        r, g, b, a = marker.getpixel((x, y))
                        if a and r and g == 0 and b == 0:
                            mask.putpixel((x, y), r)
                mask.save(target / f'{identity.lower()}_d{frame}_remap.png')
        return {'Remappable': '1', 'OriginX': str(layout['w'] // 2), 'OriginY': str(round(layout['ch'] * .72)), 'ArtFrames': str(16 if alternate else 8)}
    finally:
        raster.get, raster.PAL_U = old_get, old_palette


def provenance(get, names):
    result = {}
    for name in names:
        path, data = get(name)
        if not data:
            raise FileNotFoundError('original actor asset missing: ' + name)
        result[name] = {'source': path, 'sha256': hashlib.sha256(data).hexdigest()}
    return result
