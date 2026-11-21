"""Global AI provenance, complete references and adversarial package inputs."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ra2pack'))
from map_format import read_ini
from ra2lib import MixTree
from source_ai import build_graph, encode_graph, export_graph, profile_types, runtime_profile
from source_rules import select_source


def fixture():
    comparison = (struct.pack('<ii', 2, 3) + bytes(24)).hex()
    ai = {'taskforces': {'0': 'force'}, 'force': {'name': 'V3 and escort', '0': '2,V3'},
          'scripttypes': {'0': 'script'}, 'script': {'name': 'preserve slots', '0': '58,131073', '2': '6,1'},
          'teamtypes': {'0': 'team'}, 'team': {'house': '<none>', 'script': 'script', 'taskforce': 'force',
                                           'reinforce': 'yes', 'customflag': 'keep me'},
          'aitriggertypes': {'trigger': f'siege,team,British,1,1,V3,{comparison},50,10,70,1,0,1,0,<none>,0,1,1'},
          'aitriggertypesenable': {'trigger': 'no'}, 'customsection': {'customkey': 'uninterpreted'}}
    rules = {'countries': {'0': 'British'}, 'british': {'side': 'GDI'}, 'sides': {'gdi': 'British'},
             'buildingtypes': {'0': 'UNDEFINED', '1': 'GAWEAP', '2': 'GAWEAP'}, 'gaweap': {'name': 'factory'},
             'vehicletypes': {'0': 'V3'}, 'v3': {}, 'general': {'teamdelays': '2000,2500,3500'}}
    actors = {'v3': {'index': 17, 'building': False}, 'gaweap': {'index': 18, 'building': True}}
    return ai, rules, actors


class SourceAIContracts(unittest.TestCase):
    def test_keeps_global_country_flags_sparse_scripts_and_comparison_order(self):
        ai, rules, actors = fixture()
        g = build_graph(ai, rules, actors)
        self.assertEqual(g['taskForces'][0]['units'][0]['nativeType'], 17)
        self.assertEqual([step['slot'] for step in g['scripts'][0]['steps']], [0, 2])
        self.assertEqual(g['scripts'][0]['steps'][0]['building'], 'GAWEAP')
        self.assertEqual(g['scripts'][0]['steps'][0]['buildingNativeType'], 18)
        self.assertEqual(len(g['buildingDeclarations']), 3)
        self.assertEqual(len(g['buildingTypes']), 2)
        self.assertEqual(g['buildingDeclarations'][2]['typeIndex'], 1)
        self.assertFalse(g['buildingTypes'][0]['sourceDefined'])
        self.assertEqual(g['teams'][0]['fields'], ai['team'])
        trigger = g['triggers'][0]
        self.assertEqual((trigger['owner'], trigger['country'], trigger['operand'], trigger['comparison']), ('British', 0, 2, 3))
        self.assertEqual(trigger['difficulty'], [False, True, True])
        self.assertEqual(trigger['conditionNativeType'], 17)
        self.assertTrue(trigger['globalDefaultEnabled'])
        self.assertEqual(g['explicitEnableRows'], {'trigger': 'no'})
        self.assertEqual(g['rawGlobalSections'], ai)

    def test_rejects_missing_references_and_unavailable_actors(self):
        for section, key, value, error in [('team', 'script', 'absent', 'unresolved reference'),
                                         ('team', 'taskforce', 'absent', 'unresolved reference'),
                                         ('force', '0', '2,missing', 'unavailable mobile'),
                                         ('script', '0', '58,99', 'building script operand'),
                                         ('script', '2', '6,2', 'missing occupied slot')]:
            with self.subTest(section=section, key=key):
                ai, rules, actors = fixture()
                ai[section][key] = value
                with self.assertRaisesRegex(ValueError, error):
                    build_graph(ai, rules, actors)
        ai, rules, actors = fixture()
        for broken in ({}, {'v3': {'index': 17, 'building': True}}):
            with self.assertRaisesRegex(ValueError, 'unavailable mobile'):
                build_graph(ai, rules, broken)

    def test_missing_runtime_criterion_or_script_building_is_rejected(self):
        ai, rules, actors = fixture()
        del actors['gaweap']
        with self.assertRaisesRegex(ValueError, 'unavailable script building'):
            build_graph(ai, rules, actors)
        ai, rules, actors = fixture()
        rules['vehicletypes']['1'] = 'MISSING'
        f = ai['aitriggertypes']['trigger'].split(',')
        f[5] = 'MISSING'
        ai['aitriggertypes']['trigger'] = ','.join(f)
        with self.assertRaisesRegex(ValueError, 'unavailable condition object'):
            build_graph(ai, rules, actors)

    def test_rejects_duplicate_records_and_taskforce_holes(self):
        ai, rules, actors = fixture()
        ai['taskforces']['1'] = 'FORCE'
        with self.assertRaisesRegex(ValueError, 'duplicate record'):
            build_graph(ai, rules, actors)
        ai, rules, actors = fixture()
        ai['force']['2'] = ai['force'].pop('0')
        with self.assertRaisesRegex(ValueError, 'contiguous occupied slots'):
            build_graph(ai, rules, actors)

    def test_rejects_bad_trigger_metadata(self):
        cases = [(2, 'missing-country', 'unresolved reference'), (4, '8', 'outside'),
                 (5, 'missing-actor', 'condition object'), (6, '00', '32 bytes'),
                 (6, '0000000006000000' + '00' * 24, 'comparison operator'),
                 (7, 'nan', 'weight bounds'), (7, 'inf', 'weight bounds'),
                 (8, '71', 'weight bounds'), (12, '9', 'outside'), (17, 'perhaps', 'boolean'),
                 (14, 'missing-team', 'unresolved reference')]
        for index, value, error in cases:
            with self.subTest(index=index, value=value):
                ai, rules, actors = fixture()
                f = ai['aitriggertypes']['trigger'].split(',')
                f[index] = value
                ai['aitriggertypes']['trigger'] = ','.join(f)
                with self.assertRaisesRegex(ValueError, error):
                    build_graph(ai, rules, actors)

    def test_retains_unknown_actions_and_reserved_comparison_bytes(self):
        ai, rules, actors = fixture()
        ai['script']['2'] = '255,-1'
        f = ai['aitriggertypes']['trigger'].split(',')
        f[6] = (struct.pack('<ii', -3, 5) + bytes(range(24))).hex()
        ai['aitriggertypes']['trigger'] = ','.join(f)
        graph = build_graph(ai, rules, actors)
        self.assertEqual(graph['scripts'][0]['steps'][1]['action'], 255)
        self.assertEqual(graph['triggers'][0]['operand'], -3)
        self.assertEqual(graph['triggers'][0]['comparisonBytes'], f[6])
        self.assertEqual(json.loads(encode_graph(graph))['summary']['actions']['255'], 1)

    def test_source_selection_never_crosses_editions_and_rejects_ambiguity(self):
        class Archives:
            def __init__(self, rows):
                self.rows = rows
                self.requested = []
            def find_all(self, filename):
                self.requested.append(filename)
                return self.rows
        archive = Archives([('ra2.mix/local.mix', b'ra2'), ('ra2md.mix/localmd.mix', b'yr')])
        self.assertEqual(select_source(archive, 'ra2', 'ai'), ('ra2.mix/local.mix', b'ra2', 'ra2-local-base'))
        self.assertEqual(select_source(archive, 'yr', 'ai'), ('ra2md.mix/localmd.mix', b'yr', 'yr-local-base'))
        self.assertEqual(archive.requested, ['ai.ini', 'aimd.ini'])
        patched = Archives(archive.rows + [('expandmd01.mix', b'patch')])
        self.assertEqual(select_source(patched, 'yr', 'ai'), ('expandmd01.mix', b'patch', 'yr-expandmd01'))
        foreign_nested = Archives(archive.rows + [('ra2.mix/expandmd01.mix', b'wrong'), ('fake-expandmd01.mix', b'wrong')])
        self.assertEqual(select_source(foreign_nested, 'yr', 'ai'), ('ra2md.mix/localmd.mix', b'yr', 'yr-local-base'))
        for rows in [[('ra2.mix/local.mix', b'wrong')], [('ra2md.mix/localmd.mix', b'1'), ('ra2md.mix/localmd.mix', b'2')],
                     [('expandmd01.mix', b'1'), ('expandmd01.mix', b'2')]]:
            with self.assertRaisesRegex(ValueError, 'unique original source'):
                select_source(Archives(rows), 'yr', 'ai')
        with self.assertRaisesRegex(ValueError, 'unknown original rules edition'):
            select_source(archive, 'typo', 'ai')

    def test_profile_must_match_original_rules_and_edition(self):
        raw = b'[Scene]\nMode=Rules\nRulesId=ra2-local-base\nSourceSHA256=expected\nTypes=1\n[Type.0]\nId=V3\nBuilding=0\n'
        self.assertEqual(profile_types(raw, 'ra2-local-base', 'expected')['v3']['index'], 0)
        for edition, digest in [('yr-local-base', 'expected'), ('ra2-local-base', 'wrong')]:
            with self.assertRaisesRegex(ValueError, 'exact original rules'):
                profile_types(raw, edition, digest)


class OriginalAIIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tree = MixTree()
        if not cls.tree.find_all('ai.ini') or not cls.tree.find_all('aimd.ini'):
            raise unittest.SkipTest('original edition-local AI archives required')

    def test_both_complete_original_graphs_and_repeatable_exports(self):
        for edition, expected in [('ra2', (81, 52, 103, 107)), ('yr', (132, 88, 163, 165))]:
            with self.subTest(edition=edition):
                profile = ROOT / f'assets/rules/profiles/{edition}.ini'
                graph = export_graph(self.tree, edition, profile)
                again = export_graph(self.tree, edition, profile)
                self.assertEqual(encode_graph(graph), encode_graph(again))
                source, raw, tag = select_source(self.tree, edition, 'ai')
                original = read_ini(raw)
                self.assertEqual(graph['source']['archive'], source)
                self.assertEqual(graph['source']['sha256'], hashlib.sha256(raw).hexdigest())
                self.assertFalse(graph['runtimeEnabled'])
                g = graph['graph']
                self.assertEqual(g['rawGlobalSections'], original)
                if tag.endswith('-local-base'):
                    self.assertEqual(tuple(len(g[key]) for key in ('taskForces', 'scripts', 'teams', 'triggers')), expected)
                for table, key in [('taskforces', 'taskForces'), ('scripttypes', 'scripts'), ('teamtypes', 'teams')]:
                    self.assertEqual([row['id'] for row in g[key]], list(original[table].values()))
                    for row in g[key]:
                        self.assertEqual(row['fields'], original[row['id'].lower()])
                for trigger in g['triggers']:
                    self.assertEqual(trigger['raw'], original['aitriggertypes'][trigger['id']])
                    self.assertTrue(trigger['globalDefaultEnabled'])
                    if trigger['country'] >= 0:
                        self.assertEqual(g['countries'][trigger['country']]['id'].lower(), trigger['owner'].lower())
                force_types = {unit['object'].upper() for force in g['taskForces'] for unit in force['units']}
                self.assertIn('V3', force_types)
                if edition == 'yr':
                    self.assertIn('BFRT', force_types)
                    self.assertTrue({61, 62, 63} <= set(g['summary']['actions']))
                # The actual RA2 graph uses action58 too. Do not discard it
                # because an editor's description labels it as YR-only.
                self.assertIn(58, g['summary']['actions'])
                self.assertTrue({5, 6, 7} <= set(g['summary']['conditions']))
                duplicate = next(row for row in g['buildingDeclarations'] if row['sourceKey'] == '241')
                self.assertEqual(duplicate['id'], 'NAPSYA')
                self.assertEqual(duplicate['typeIndex'],
                                 next(i for i, row in enumerate(g['buildingTypes']) if row['id'] == 'NAPSYA'))

    def test_opt_in_runtime_profile_preserves_original_records_and_default_file(self):
        import configparser
        for edition in ['ra2', 'yr']:
            with self.subTest(edition=edition):
                profile = ROOT / f'assets/rules/profiles/{edition}.ini'
                original = profile.read_bytes()
                exported = runtime_profile(self.tree, edition, profile)
                self.assertEqual(exported, runtime_profile(self.tree, edition, profile))
                ini = configparser.ConfigParser(interpolation=None)
                ini.read_string(exported.decode('utf-8'))
                self.assertEqual(ini['Scene']['Version'], '41')
                self.assertEqual(ini['NativeProduction']['Version'], '5')
                self.assertNotIn('OreValue', ini['NativeProduction'])
                self.assertNotIn('GemValue', ini['NativeProduction'])
                self.assertNotIn('HarvesterLoadRate', ini['NativeProduction'])
                self.assertEqual(dict(ini['NativeEvent.houses']), {})
                self.assertEqual(dict(ini['NativeEvent.triggers']), {})
                self.assertEqual(ini['NativeEvents']['PlayerHouse'], '-1')
                self.assertEqual(ini['NativeEvents']['AIOnly'], '1')
                self.assertEqual(ini['NativeAI']['Version'], '5')
                self.assertEqual(ini['NativeEvents']['Version'], '8')
                self.assertEqual(ini['NativeEvents']['RelaxedStrayDistance'], '3.0')
                self.assertEqual(ini['NativeAI']['OriginalTeamControl'], '1')
                self.assertEqual(ini['NativeAI']['OriginalNearbyGeometry'], '1')
                self.assertEqual(ini['NativeAI']['OriginalSourceCoordinates'], '1')
                self.assertEqual(ini['NativeAI.NearbySpeeds']['E2'], '0')
                self.assertEqual(ini['NativeAI.NearbySpeeds']['V3'], '1')
                self.assertEqual(ini['NativeAI.NearbySpeeds']['ORCA'], '4')
                self.assertTrue(ini['NativeAI.NearbyWalls'])
                self.assertEqual(ini['NativeAI.Leadership']['GHOST'], '8')
                self.assertEqual(ini['NativeAI.Leadership']['V3'], '5')
                self.assertEqual(ini['NativeAI']['Global'], '1')
                self.assertEqual(ini['NativeAI']['AISafeDistance'], '20')
                self.assertEqual(ini['NativeAI.CountrySides']['British'], '1')
                self.assertEqual(ini['NativeAI.CountrySides']['Russians'], '2')
                if edition == 'yr':
                    self.assertEqual(ini['NativeAI.CountrySides']['YuriCountry'], '3')
                ai = read_ini(select_source(self.tree, edition, 'ai')[1])
                for table in ['teamtypes', 'taskforces', 'scripttypes']:
                    for identity in ai[table].values():
                        self.assertEqual(dict(ini['NativeEvent.record.' + identity.lower()]), ai[identity.lower()])
                self.assertEqual(profile.read_bytes(), original)


if __name__ == '__main__':
    unittest.main(verbosity=2)
