"""Export the complete edition-local global AI graph for runtime integration.

This is a data package, not an enabled skirmish strategy. Keep raw fields beside
resolved references so importing the graph cannot silently discard team flags,
script slots, country restrictions or unknown comparison bytes.
"""
import argparse
from collections import Counter
import configparser
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct

from map_format import read_ini
from ra2lib import MixTree
from source_rules import select_source


def integer(text, low, high, context):
    if not re.fullmatch(r"-?[0-9]+", text):
        raise ValueError(f"{context}: invalid integer {text!r}")
    value = int(text)
    if not low <= value <= high:
        raise ValueError(f"{context}: integer outside [{low}, {high}]")
    return value


def fields(text, count, context):
    result = [part.strip() for part in text.split(',')]
    if len(result) != count:
        raise ValueError(f"{context}: expected {count} fields")
    return result


def boolean(text, context):
    lowered = text.lower()
    if lowered not in ('yes', 'true', '1', 'no', 'false', '0'):
        raise ValueError(f"{context}: invalid boolean {text!r}")
    return lowered in ('yes', 'true', '1')


def records(source, table):
    rows, seen = [], set()
    if table not in source or len(source[table]) > 4096:
        raise ValueError(f"missing or oversized [{table}]")
    for key, identity in source[table].items():
        integer(key, 0, 4095, table)
        normalized = identity.lower()
        if not identity or len(identity) > 64 or normalized in seen or normalized not in source:
            raise ValueError(f"{table}: missing or duplicate record {identity!r}")
        seen.add(normalized)
        rows.append({'id': identity, 'sourceKey': key, 'fields': dict(source[normalized])})
    return rows


def reference(identity, rows, context, allow_none=False):
    if allow_none and identity.lower() == '<none>':
        return -1
    for index, row in enumerate(rows):
        if row['id'].lower() == identity.lower():
            return index
    raise ValueError(f"{context}: unresolved reference {identity!r}")


def type_registry(rules, table):
    """Keep every declaration while resolving repeated IDs to one type slot.

    Match the existing native-event exporter: first declaration owns the slot.
    Original registries also declare types without a separate definition block.
    Their declaration still occupies a slot; dropping it shifts script operands.
    """
    types, declarations, seen = [], [], {}
    if table not in rules or len(rules[table]) > 4096:
        raise ValueError(f"missing or oversized [{table}]")
    for key, identity in rules[table].items():
        integer(key, 0, 4095, table)
        if not identity or len(identity) > 64:
            raise ValueError(f"{table}: empty or oversized type ID")
        normalized = identity.lower()
        if normalized not in seen:
            seen[normalized] = len(types)
            types.append({'id': identity, 'sourceKey': key, 'sourceDefined': normalized in rules})
        declarations.append({'id': identity, 'sourceKey': key, 'typeIndex': seen[normalized]})
    return types, declarations


def profile_types(raw, edition_tag, rules_hash):
    ini = configparser.ConfigParser(interpolation=None)
    ini.read_string(raw.decode('utf-8-sig'))
    scene = ini['Scene']
    if scene.get('Mode') != 'Rules' or scene.get('RulesId') != edition_tag or scene.get('SourceSHA256') != rules_hash:
        raise ValueError('profile must bind the selected edition and exact original rules')
    count = integer(scene.get('Types', ''), 1, 4096, 'profile Types')
    result = {}
    for index in range(count):
        row = ini['Type.' + str(index)]
        identity = row.get('Id', '').lower()
        if not identity or identity in result:
            raise ValueError('profile contains an empty or duplicate actor ID')
        result[identity] = {'index': index, 'building': boolean(row.get('Building', ''), identity)}
    return result


def build_graph(ai, rules, actors):
    forces = records(ai, 'taskforces')
    scripts = records(ai, 'scripttypes')
    teams = records(ai, 'teamtypes')
    countries = records(rules, 'countries')
    # Building script operands refer to the original registry, not the much
    # smaller executable Type table. Retain both order and original list keys.
    buildings, building_declarations = type_registry(rules, 'buildingtypes')
    mobile_ids = {identity.lower() for table in ('vehicletypes', 'infantrytypes', 'aircrafttypes')
                  for identity in rules.get(table, {}).values()}
    all_types = mobile_ids | {row['id'].lower() for row in buildings}
    for force in forces:
        force['units'] = []
        for slot, text in force['fields'].items():
            if not slot.isdigit():
                continue
            integer(slot, 0, 63, force['id'])
            amount, identity = fields(text, 2, force['id'])
            amount = integer(amount, 1, 256, force['id'])
            actor = actors.get(identity.lower())
            if identity.lower() not in mobile_ids or actor is None or actor['building']:
                raise ValueError(f"{force['id']}: unavailable mobile actor {identity!r}")
            force['units'].append({'slot': int(slot), 'count': amount, 'object': identity, 'nativeType': actor['index']})
        if not force['units'] or [unit['slot'] for unit in force['units']] != list(range(len(force['units']))):
            raise ValueError(f"{force['id']}: task force must have contiguous occupied slots")
    for script in scripts:
        script['steps'] = []
        for slot, text in script['fields'].items():
            if not slot.isdigit():
                continue
            integer(slot, 0, 49, script['id'])
            action, argument = fields(text, 2, script['id'])
            step = {'slot': int(slot), 'action': integer(action, 0, 255, script['id']),
                    'argument': integer(argument, -1, 100000000, script['id'])}
            if step['action'] in (46, 47, 58):
                position = step['argument'] & 0xffff
                if position >= len(buildings):
                    raise ValueError(f"{script['id']}: unresolved building script operand {argument}")
                step['building'] = buildings[position]['id']
                actor = actors.get(step['building'].lower())
                if actor is None or not actor['building']:
                    raise ValueError(f"{script['id']}: unavailable script building {step['building']!r}")
                step['buildingNativeType'] = actor['index']
            if step['action'] in (17, 18):
                target = scripts if step['action'] == 17 else teams
                if not 0 <= step['argument'] < len(target):
                    raise ValueError(f"{script['id']}: unresolved script/team operand {argument}")
            script['steps'].append(step)
        slots = [step['slot'] for step in script['steps']]
        if not slots or slots != sorted(set(slots)):
            raise ValueError(f"{script['id']}: script slots must retain increasing source order")
        for step in script['steps']:
            if step['action'] == 6 and step['argument'] - 1 not in slots:
                raise ValueError(f"{script['id']}: jump references a missing occupied slot")
    for team in teams:
        row = team['fields']
        team['script'] = reference(row.get('script', ''), scripts, team['id'])
        team['taskForce'] = reference(row.get('taskforce', ''), forces, team['id'])
        if row.get('house', '').lower() not in ('<none>', '<all>'):
            reference(row['house'], countries, team['id'])
        # Flags stay raw and complete. Do not silently translate Reinforce into
        # free spawns, or strip flags which the current executor cannot handle.
    triggers = []
    for identity, raw in ai.get('aitriggertypes', {}).items():
        f = fields(raw, 18, identity)
        owner = f[2]
        country = -1 if owner.lower() == '<all>' else reference(owner, countries, identity)
        condition = integer(f[4], -1, 7, identity)
        if condition in (0, 1, 7) and f[5].lower() not in all_types:
            raise ValueError(f"{identity}: unresolved condition object {f[5]!r}")
        condition_type = -1
        if condition in (0, 1, 7):
            actor = actors.get(f[5].lower())
            if actor is None:
                raise ValueError(f"{identity}: unavailable condition object {f[5]!r}")
            condition_type = actor['index']
        if not re.fullmatch('[0-9a-fA-F]{64}', f[6]):
            raise ValueError(f"{identity}: comparison block must contain 32 bytes")
        # Calibrated against local RA2 instructions, not YRpp field names.
        operand, comparison = struct.unpack('<ii', bytes.fromhex(f[6])[:8])
        if not 0 <= comparison <= 5:
            raise ValueError(f"{identity}: unsupported comparison operator {comparison}")
        try:
            weight, minimum, maximum = map(float, f[7:10])
        except ValueError as error:
            raise ValueError(f"{identity}: invalid weight") from error
        if not all(math.isfinite(v) and 0 <= v <= 1000000 for v in (weight, minimum, maximum)) or maximum < minimum:
            raise ValueError(f"{identity}: invalid weight bounds")
        triggers.append({'id': identity, 'raw': raw, 'name': f[0],
                         'firstTeam': reference(f[1], teams, identity),
                         'secondTeam': reference(f[14], teams, identity, allow_none=True),
                         'owner': owner, 'country': country, 'techLevel': integer(f[3], -1, 100, identity),
                         'condition': condition, 'conditionObject': f[5], 'conditionNativeType': condition_type,
                         'operand': operand, 'comparison': comparison,
                         'comparisonBytes': f[6], 'weight': weight, 'minimum': minimum, 'maximum': maximum,
                         'skirmish': boolean(f[10], identity), 'sourceField11': integer(f[11], 0, 100, identity),
                         'side': integer(f[12], 0, len(rules.get('sides', {})), identity),
                         'baseDefense': boolean(f[13], identity),
                         'difficulty': [boolean(value, identity) for value in f[15:18]],
                         'globalDefaultEnabled': True})
    if not triggers or len(triggers) > 4096:
        raise ValueError('missing or oversized AI trigger table')
    enabled = ai.get('aitriggertypesenable', {})
    for identity, value in enabled.items():
        reference(identity, triggers, 'AITriggerTypesEnable')
        boolean(value, identity)
    actions = Counter(step['action'] for script in scripts for step in script['steps'])
    conditions = Counter(row['condition'] for row in triggers)
    return {'taskForces': forces, 'scripts': scripts, 'teams': teams, 'triggers': triggers,
            'countries': countries, 'buildingTypes': buildings, 'buildingDeclarations': building_declarations,
            'sides': dict(rules.get('sides', {})),
            'general': dict(rules.get('general', {})), 'aiControls': dict(rules.get('ai', {})),
            'difficultyRules': {name: dict(rules.get(name, {})) for name in ('easy', 'normal', 'difficult')},
            'rawGlobalSections': {name: dict(row) for name, row in ai.items()}, 'explicitEnableRows': dict(enabled),
            'summary': {'taskForces': len(forces), 'scripts': len(scripts), 'teams': len(teams), 'triggers': len(triggers),
                        'actions': dict(sorted(actions.items())), 'conditions': dict(sorted(conditions.items()))}}


def export_graph(tree, edition, profile):
    source, raw, source_tag = select_source(tree, edition, 'ai')
    rules_source, rules_raw, rules_tag = select_source(tree, edition)
    profile_raw = Path(profile).read_bytes()
    rules_hash = hashlib.sha256(rules_raw).hexdigest()
    actors = profile_types(profile_raw, rules_tag, rules_hash)
    graph = build_graph(read_ini(raw), read_ini(rules_raw), actors)
    return {'schema': 'OpenRA2.SourceGlobalAI.1', 'edition': edition, 'runtimeEnabled': False,
            'source': {'file': 'ai.ini' if edition == 'ra2' else 'aimd.ini', 'archive': source,
                       'edition': source_tag, 'sha256': hashlib.sha256(raw).hexdigest()},
            'rules': {'archive': rules_source, 'edition': rules_tag, 'sha256': rules_hash},
            'profile': {'sha256': hashlib.sha256(profile_raw).hexdigest(), 'types': len(actors)},
            'globalEnablePolicy': 'declarations-enabled; explicit source list retained separately',
            'graph': graph}


def encode_graph(graph):
    return (json.dumps(graph, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')


def runtime_profile(tree, edition, profile):
    """Opt-in Scene40 integration profile; ordinary shipped profiles stay put."""
    from gen_native_events import export_native_demolition
    from gen_native_movement import SPEEDS
    package = export_graph(tree, edition, profile)
    graph = package['graph']
    rules = read_ini(select_source(tree, edition)[1])
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(profile, encoding='utf-8-sig')
    ini['Scene']['Version'] = '41'
    # Scene38-41 are frozen experimental contracts, including the historical
    # project credit scale. Ordinary Scene43 economy is a separate cohort.
    ini['NativeProduction']['Version'] = '5'
    ini['NativeProduction'].pop('OreValue', None)
    ini['NativeProduction'].pop('GemValue', None)
    ini['NativeProduction'].pop('HarvesterLoadRate', None)
    ini['NativeProduction'].pop('HarvesterDumpFrames', None)
    ini['NativeProduction'].pop('PurifierBonus', None)
    for section in ini.sections():
        if section.startswith('NativeProduction.Type.'):
            ini[section].pop('ROT', None)
    for name in ('triggers', 'events', 'actions', 'tags', 'celltags', 'waypoints', 'houses', 'houseNumbers', 'variableNames'):
        ini['NativeEvent.' + name] = {}
    for table, rows in [('taskforces', graph['taskForces']), ('scripttypes', graph['scripts']), ('teamtypes', graph['teams'])]:
        ini['NativeEvent.' + table] = {row['sourceKey']: row['id'] for row in rows}
        for row in rows:
            ini['NativeEvent.record.' + row['id'].lower()] = row['fields']
    ini['NativeEvent.buildingTypes'] = {str(i): row['id'] for i, row in enumerate(graph['buildingTypes'])}
    for table, target in [('vehicletypes', 'vehicleTypes'), ('infantrytypes', 'infantryTypes'), ('aircrafttypes', 'aircraftTypes')]:
        types = list(dict.fromkeys(value.upper() for _, value in sorted(rules[table].items(), key=lambda row: int(row[0]))))
        ini['NativeEvent.' + target] = {str(i): value for i, value in enumerate(types)}
    export_native_demolition(ini, {}, rules)
    ini['NativeEvents'] = {'Version': '8', 'AIOnly': '1', 'Triggers': '0', 'Tags': '0', 'PlayerHouse': '-1',
                           'StrayDistance': graph['general']['stray'], 'RelaxedStrayDistance': graph['general']['relaxedstray'],
                           'FogOfWar': '1', 'RevealRadius': '9'}
    header = {'Version': '5', 'Global': '1', 'IgnoreGlobalAITriggers': '0', 'OriginalTeamControl': '1', 'OriginalNearbyGeometry': '1', 'OriginalSourceCoordinates': '1',
              'BaseBuildings': rules['ai']['buildconst'],
              'SourceArchive': package['source']['archive'], 'SourceSHA256': package['source']['sha256'],
              'UseMinDefenseRule': str(int(graph['general'].get('usemindefenserule', 'yes').lower() == 'yes')),
              'AIMinorSuperReadyPercent': graph['general']['aiminorsuperreadypercent'],
              'AISafeDistance': graph['general']['aisafedistance']}
    for key in ('TeamDelays', 'TotalAITeamCap', 'MinimumAIDefensiveTeams', 'MaximumAIDefensiveTeams', 'FillEarliestTeamProbability'):
        header[key] = graph['general'][key.lower()]
    for key, fallback in [('AITriggerSuccessWeightDelta', '1'), ('AITriggerFailureWeightDelta', '-1'), ('AITriggerTrackRecordCoefficient', '1')]:
        header[key] = graph['general'].get(key.lower(), fallback)
    ini['NativeAI'] = header
    ini['NativeAI.Leadership'] = {row['Id']: str(integer(rules.get(row['Id'].lower(), {}).get('leadershiprating', '5'), -1000000, 1000000, row['Id']))
                                 for name in ini.sections() if name.startswith('Type.') and (row := ini[name]).get('Building') == '0'}
    infantry = {name.upper() for name in rules.get('infantrytypes', {}).values()}
    aircraft = {name.upper() for name in rules.get('aircrafttypes', {}).values()}
    speeds = {}
    for section in ini.sections():
        row = ini[section]
        if not section.startswith('Type.') or row.get('Building') != '0':
            continue
        actor = row['Id'];original = rules.get(actor.lower(), {})
        default = 'Foot' if actor in infantry else 'Winged' if actor in aircraft else 'Track' if boolean(original.get('crusher', 'no'), actor) else 'Wheel'
        speed = row.get('OriginalSpeedType', original.get('speedtype', default))
        indices = {name.lower(): index for index, name in enumerate(SPEEDS)}
        if speed.lower() not in indices:
            raise ValueError((actor, 'unsupported source Nearby SpeedType', speed))
        speeds[actor] = str(indices[speed.lower()])
    ini['NativeAI.NearbySpeeds'] = speeds
    ini['NativeAI.NearbyWalls'] = {
        str(index): str(int(boolean(rules.get(actor.lower(), {}).get('crushable', 'no'), actor)))
        for text, actor in rules.get('overlaytypes', {}).items()
        if (index := integer(text, 0, 100000, 'OverlayTypes')) < 255
        and boolean(rules.get(actor.lower(), {}).get('wall', 'no'), actor)}
    ini['NativeAI.Triggers'] = {row['id']: row['raw'] for row in graph['triggers']}
    ini['NativeAI.Enabled'] = graph['explicitEnableRows']
    sides = list(graph['sides'])
    ini['NativeAI.CountrySides'] = {row['id']: str(sides.index(row['fields']['side'].lower()) + 1) for row in graph['countries']}
    stream = io.StringIO()
    ini.write(stream)
    return stream.getvalue().encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edition', choices=('ra2', 'yr'))
    parser.add_argument('--profile', required=True, type=Path, help='exact executable rules profile to bind actor references')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--runtime-profile', action='store_true', help='write an experimental Scene38 runtime profile; does not replace the default rules')
    args = parser.parse_args()
    tree = MixTree()
    graph = export_graph(tree, args.edition, args.profile)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(runtime_profile(tree, args.edition, args.profile) if args.runtime_profile else encode_graph(graph))
    print('SOURCE GLOBAL AI', args.edition, graph['graph']['summary'],
          'experimental Scene38 runtime profile; default profiles unchanged' if args.runtime_profile else 'runtimeEnabled=False')


if __name__ == '__main__':
    main()
