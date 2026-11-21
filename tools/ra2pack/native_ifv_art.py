"""Copy the selected edition's original gunner hull and numbered turrets."""
import hashlib
import json
from pathlib import Path
from ra2lib import Vxl, Hva


def export_ifv_art(ini, get, directory):
    directory = Path(directory)
    manifest = {}
    for section in ini.sections():
        row = ini[section]
        if not section.startswith('Type.') or row.get('IFVGunner') != '1':
            continue
        image, count = row['IFVImage'], int(row['IFVTurrets'])
        if not image or len(image) > 64 or not all(c.isascii() and (c.isalnum() or c == '_') for c in image):
            raise ValueError(('invalid gunner image', image))
        stems = [image] + [image + 'tur' + (str(i) if i else '') for i in range(count)]
        for stem in stems:
            for extension, parser in (('.vxl', Vxl), ('.hva', Hva)):
                name = stem + extension
                source, raw = get(name)
                if not raw:
                    raise FileNotFoundError(('original gunner art', name))
                parser(raw)  # Fail the export if the original resource cannot be read.
                directory.mkdir(parents=True, exist_ok=True)
                (directory / name).write_bytes(raw)
                manifest[name] = {'source': source, 'sha256': hashlib.sha256(raw).hexdigest()}
    if manifest:
        (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest
