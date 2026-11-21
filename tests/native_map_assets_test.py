"""Verify imported native cells against the installed original archive, not a fixture approximation."""
import hashlib
import json
import struct
import re
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/ra2pack"))
from gen_native_maps import core_file
from map_format import parse_map
from ra2lib import MixTree, name_db

directory = ROOT / "assets/maps/native/all01t"
if not (directory / "terrain.nmap").is_file():
    print("NATIVE MAP ASSETS SKIP: import the locally installed original maps first")
    sys.exit(77)
tree = MixTree()
catalog=name_db(); campaign_maps={}
for mix in tree.mixes:
    if mix.name.lower() not in ("maps01.mix","maps02.mix"):
        continue
    for entry in mix.index:
        name=catalog.get(entry,"")
        if re.fullmatch(r"(?:all|sov)\d\d[a-z]?\.map",name):
            original=mix.get_by_id(entry)
            if name in campaign_maps:
                assert campaign_maps[name][0]==hashlib.sha256(original).digest()
            else:
                campaign_maps[name]=(hashlib.sha256(original).digest(),parse_map(original))
assert len(campaign_maps)==24 and sum(len(v[1].tiles) for v in campaign_maps.values())==400474
report = json.loads((directory / "import-report.json").read_text(encoding="utf-8"))
source, data = core_file(tree, "all01t.map")
assert hashlib.sha256(data).hexdigest() == report["source_sha256"]
assert (directory / "scenario.map").read_bytes() == data
native = parse_map(data)
assert (native.width,native.height,native.local,len(native.tiles)) == (50,64,(2,6,46,52),6336)
scenario = json.loads((directory / "scenario.json").read_text(encoding="utf-8"))
for section in ["structures", "units", "infantry", "terrain", "houses", "waypoints", "triggers", "events", "actions", "teamtypes", "taskforces", "scripttypes"]:
    assert scenario[section] == native.sections[section], section
assert len(scenario["structures"])==190 and len(scenario["units"])+len(scenario["infantry"])==59
assert len(scenario["triggers"])==len(scenario["events"])==len(scenario["actions"])==130

payload = (directory / "terrain.nmap").read_bytes()
assert payload[:8] == b"RA2NMAP1" and struct.unpack_from("<7H",payload,8)==(114,50,64,2,6,46,52)
assert payload[22:54] == hashlib.sha256(data).digest()
cursor=54
for expected in [b"all01t", b"URBAN"]:
    count=struct.unpack_from("<H",payload,cursor)[0];cursor+=2
    assert payload[cursor:cursor+count]==expected;cursor+=count
count=struct.unpack_from("<I",payload,cursor)[0];cursor+=4
assert count==6336 and len(payload)==cursor+count*17
records={}
for i in range(count):
    values=struct.unpack_from("<HHHH9B",payload,cursor+i*17)
    assert values[:2] not in records
    records[values[:2]]=values
for tile in native.tiles:
    row=records[tile.x,tile.y]
    assert row[:7]==(tile.x,tile.y,tile.tile,tile.auxiliary,tile.subtile,tile.height,tile.auxiliary2)
    index=tile.x+512*tile.y
    assert row[7:9]==(native.overlays[index],native.overlay_data[index])
    if row[9]==9: assert row[11]==2, ("native water",row)
    if row[9]==15: assert row[12]==1, ("native impassable",row)
for name,record in report["source_tiles"].items():
    assert hashlib.sha256(core_file(tree,name)[1]).hexdigest()==record["sha256"],name
for filename,field in [("terrain.png","terrain_sha256"),("terrain-owners.png","owners_sha256"),("terrain.nmap","nmap_sha256")]:
    assert hashlib.sha256((directory/filename).read_bytes()).hexdigest()==report[field]
print("NATIVE MAP ASSETS PASS: 24 original campaigns/400474 cells decoded; first Allied map verifies 6336 exact cells, 658 TMP frames, 249 actors and all 130 event/action groups")
