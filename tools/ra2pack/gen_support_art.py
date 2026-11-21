"""Export the original parachute animation without procedural substitutes."""
import hashlib
import json
from pathlib import Path
from PIL import Image
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from source_rules import source_asset, select_source, read_ini


def export_parachute(tree, target):
    # Refuse a shared animation if the installed editions disagree.
    art_path, art_raw, _ = select_source(tree, 'yr', 'art')
    art = read_ini(art_raw)['parach']
    source, raw = source_asset(tree, 'yr', 'parach.shp')
    palette_path, palette_raw = source_asset(tree, 'yr', 'unittem.pal')
    assert source_asset(tree, 'ra2', 'parach.shp')[1] == raw
    assert source_asset(tree, 'ra2', 'unittem.pal')[1] == palette_raw
    ra2_art = read_ini(select_source(tree, 'ra2', 'art')[1])['parach']
    assert ra2_art == art
    shp = Shp(raw);palette = load_pal(palette_raw)
    assert int(art['loopstart']) == 20 and int(art['loopend']) == 39
    target = Path(target);target.mkdir(parents=True, exist_ok=True)
    frames = []
    for index in range(40):
        frame = shp.frame_pixels(index);image = Image.new('RGBA', (shp.w, shp.h))
        if frame.w and frame.h:image.alpha_composite(shp_frame_to_rgba(frame, palette, remap=False), (frame.x, frame.y))
        path = target / f'fx_parach_{index}.png';image.save(path)
        frames.append({'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    report = {'source': source, 'sha256': hashlib.sha256(raw).hexdigest(),
              'palette': palette_path, 'paletteSHA256': hashlib.sha256(palette_raw).hexdigest(),
              'artSource': art_path, 'artSHA256': hashlib.sha256(art_raw).hexdigest(), 'art': art, 'frames': frames}
    (target / 'parachute-provenance.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    report = export_parachute(MixTree(), Path(__file__).resolve().parents[2] / 'assets/sprites')
    print('Original PARACH frames:', len(report['frames']), report['source'])
