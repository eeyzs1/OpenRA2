"""Export an edition-local, recursively resolved weapon/projectile/warhead graph."""


def export_native_weapon_catalog(ini, rules, scenario):
    merge = lambda name: {**rules.get(name.lower(), {}), **scenario.get(name.lower(), {})}
    yes = lambda value: str(value).lower() in ('yes', 'true', '1')
    present = lambda value: value.lower() not in ('', 'none', '<none>')
    weapons, projectiles, warheads = [], [], []
    weapon_ids, projectile_ids, warhead_ids = {}, {}, {}

    def register(name, names, indices):
        key = name.lower()
        if key not in indices:
            indices[key] = len(names)
            names.append(name)
        return indices[key]

    actor_rows = [ini[s] for s in ini.sections() if s.startswith('Type.') and ini[s].get('Kind', '0') == '0']
    vehicles = {v.lower() for v in merge('VehicleTypes').values()}
    aircraft = {v.lower() for v in merge('AircraftTypes').values()}
    ifv_modes = ['normal', 'repair', 'machinegun', 'flak', 'pistol', 'sniper', 'shock', 'explode',
                 'brainblast', 'radcannon', 'chrono', 'terroristexplode', 'cow']
    if ini['Scene']['RulesId'].startswith('yr'):
        ifv_modes += ['initiate', 'virus', 'yuriprime', 'guardian']
    combat = merge('CombatDamage')
    yr = ini['Scene']['RulesId'].startswith('yr')
    ini['NativeCrew'] = {'Version': '2', 'OccupyMode': str(int(yr)), 'OpenRange': combat.get('opentoppedrangebonus', '0'),
                         'WarpDistance': combat.get('opentoppedwarpdistance', '0'),
                         'OpenDamage': combat.get('opentoppeddamagemultiplier', '1'),
                         'OccupyDamage': combat.get('occupydamagemultiplier', '1'),
                         'OccupyROF': combat.get('occupyrofmultiplier', '1'),
                         'OccupyRange': combat.get('occupyweaponrange', '5')}
    for row in actor_rows:
        actor = merge(row['Id'])
        row['Warpable'] = str(int(yes(actor.get('warpable', 'yes'))))
        occupier = yes(actor.get('occupier', 'no'))
        row.update(CrewOpenTopped=str(int(yes(actor.get('opentopped', 'no')))), CrewOccupier=str(int(occupier)),
                   CrewOccupyWeapon='-1', CrewEliteOccupyWeapon='-1')
        transport = int(actor.get('opentransportweapon', '-1'))
        if not -1 <= transport < 32:
            raise ValueError(('unsupported source transport weapon slot', row['Id'], transport))
        row.update(CrewTransportWeapon=str(transport), CrewTransportReference='-1', CrewEliteTransportReference='-1')
        if transport >= 0:
            numbered = yes(actor.get('gunner', 'no')) or yes(actor.get('isgattling', 'no')) or transport >= 2
            key = f'weapon{transport+1}' if numbered else ('primary', 'secondary')[transport]
            normal = actor.get(key, '')
            elite = actor.get('elite'+key, normal)
            # Keep an explicitly empty chosen slot empty. It must not turn
            # into automatic selection, nor borrow another weapon's rules.
            for name, field in [(normal, 'CrewTransportReference'), (elite, 'CrewEliteTransportReference')]:
                if present(name):row[field] = str(register(name, weapons, weapon_ids))
        if occupier:
            normal = actor.get('occupyweapon', actor.get('primary', '')) if yr else actor.get('primary', '')
            elite = actor.get('eliteoccupyweapon', actor.get('primary', '')) if yr else actor.get('eliteprimary', normal)
            if not present(normal) or not present(elite):raise ValueError(('missing occupant weapon', row['Id']))
            row['CrewOccupyWeapon'] = str(register(normal, weapons, weapon_ids))
            row['CrewEliteOccupyWeapon'] = str(register(elite, weapons, weapon_ids))
        identity = row['Id'].lower()
        # RA2 GAME.EXE Unit::GetFireError 0x704cf7 requires the patient's
        # IsStrange virtual: !NonVehicle, landed Aircraft, or an undeployable
        # non-ConstructionYard building. YRpp documents the YR building as 1x1.
        building = row.get('Building') == '1'
        deployable = building and present(actor.get('undeploysinto', '')) and not yes(actor.get('constructionyard', 'no'))
        if ini['Scene']['RulesId'].startswith('yr'):
            deployable = deployable and row.get('W') == row.get('H') == '1'
        row['VehicleRepairable'] = str(int(deployable or identity in aircraft or identity in vehicles and not yes(actor.get('nonvehicle', 'no'))))
        for slot in range(12):
            if row.get(f'Gun{slot}Present') == '1':
                row[f'Gun{slot}Reference'] = str(register(row[f'Gun{slot}Id'], weapons, weapon_ids))
        gunner = yes(actor.get('gunner', 'no'))
        row.update(NativeIFV='2', IFVPassengerMode=actor.get('ifvmode', '0'), IFVGunner=str(int(gunner)),
                   IFVWeapons='0', IFVModes='0', IFVTurrets='0')
        for field, key in [('ImmuneToPsionicWeapons', 'immunetopsionicweapons'),
                           ('ImmuneToRadiation', 'immunetoradiation'), ('ImmuneToPoison', 'immunetopoison')]:
            row[field] = str(int(yes(actor.get(key, 'no'))))
        if gunner:
            count = int(actor['weaponcount'])
            turrets = int(actor.get('turretcount', '1'))
            if count < 1 or count > 32 or turrets < 1 or turrets > 32:
                raise ValueError(('unsupported source gunner bounds', row['Id'], count, turrets))
            row.update(IFVWeapons=str(count), IFVModes=str(len(ifv_modes)), IFVTurrets=str(turrets))
            row['IFVImage'] = actor.get('image', row['Id']).lower()
            delay = int(actor.get('burstdelay0', '-1'))
            row['IFVBurstDelayMin'], row['IFVBurstDelayMax'] = str(delay if delay >= 0 else 3), str(delay if delay >= 0 else 5)
            for slot in range(count):
                normal = actor[f'weapon{slot+1}']
                elite = actor.get(f'eliteweapon{slot+1}', normal)
                if not present(normal) or not present(elite):
                    raise ValueError(('empty source gunner slot', row['Id'], slot))
                row[f'IFVWeapon{slot}Reference'] = str(register(normal, weapons, weapon_ids))
                row[f'IFVWeapon{slot}EliteReference'] = str(register(elite, weapons, weapon_ids))
            for index, mode in enumerate(ifv_modes):
                weapon, turret = int(actor[mode+'turretweapon']), int(actor[mode+'turretindex'])
                if not 0 <= weapon < count or not 0 <= turret < turrets:
                    raise ValueError(('invalid source gunner mapping', row['Id'], mode, weapon, turret))
                row[f'IFVMode{index}Weapon'], row[f'IFVMode{index}Turret'] = str(weapon), str(turret)
        death = actor.get('deathweapon', '')
        if present(death):
            register(death, weapons, weapon_ids)

    index = 0
    while index < len(weapons):
        name = weapons[index]
        weapon = merge(name)
        if not weapon or 'projectile' not in weapon or 'warhead' not in weapon:
            raise ValueError(('incomplete original weapon', name))
        projectile = merge(weapon['projectile'])
        if not projectile or not merge(weapon['warhead']):
            raise ValueError(('missing original projectile/warhead', name))
        register(weapon['projectile'], projectiles, projectile_ids)
        register(weapon['warhead'], warheads, warhead_ids)
        for key in ('shrapnelweapon', 'airburstweapon'):
            child = projectile.get(key, '')
            if present(child):
                register(child, weapons, weapon_ids)
        index += 1
        if len(weapons) > 2048 or len(projectiles) > 1024 or len(warheads) > 1024:
            raise ValueError('original weapon catalog exceeds runtime bounds')

    ini['NativeWeapons'] = {'Version': '3', 'Weapons': str(len(weapons)),
                            'Projectiles': str(len(projectiles)), 'Warheads': str(len(warheads))}
    for index, name in enumerate(projectiles):
        raw = merge(name)
        row = {'Id': name, 'Image': raw.get('image', 'none'), 'ROT': raw.get('rot', '0'),
               'ShrapnelWeapon': str(weapon_ids.get(raw.get('shrapnelweapon', '').lower(), -1)),
               'ShrapnelCount': raw.get('shrapnelcount', '0'),
               'AirburstWeapon': str(weapon_ids.get(raw.get('airburstweapon', '').lower(), -1)),
               'Cluster': raw.get('cluster', '1')}
        for field, key, default in [('Arcing', 'arcing', 'no'), ('Inviso', 'inviso', 'no'),
                                    ('Vertical', 'vertical', 'no'), ('Inaccurate', 'inaccurate', 'no'),
                                    ('Proximity', 'proximity', 'no'), ('AA', 'aa', 'no'), ('AG', 'ag', 'yes'),
                                    ('Level', 'level', 'no'), ('FlakScatter', 'flakscatter', 'no'), ('Airburst', 'airburst', 'no')]:
            row[field] = str(int(yes(raw.get(key, default))))
        ini[f'NativeProjectile.{index}'] = row
    for index, name in enumerate(warheads):
        raw = merge(name)
        ini[f'NativeWarhead.{index}'] = {'Id': name, 'Verses': raw.get('verses', ','.join(['100%'] * 11)),
            'Spread': raw.get('cellspread', '0'), 'PercentAtMax': raw.get('percentatmax', '1'),
            'Temporal': str(int(yes(raw.get('temporal', 'no')))),
            'AffectsAllies': str(int(yes(raw.get('affectsallies', 'yes')))),
            'PsychicDamage': str(int(yes(raw.get('psychicdamage', 'no')))),
            'Radiation': str(int(yes(raw.get('radiation', 'no')))), 'InfantryDeath': raw.get('infdeath', '0')}
    for index, name in enumerate(weapons):
        raw = merge(name)
        color = [int(value.strip()) for value in raw.get('laserinnercolor', '255,0,0').split(',')]
        if len(color) != 3 or any(value < 0 or value > 255 for value in color):
            raise ValueError(('invalid laser color', name))
        row = {'Id': name, 'Projectile': str(projectile_ids[raw['projectile'].lower()]),
               'Warhead': str(warhead_ids[raw['warhead'].lower()]), 'Damage': raw.get('damage', '0'),
               'ROF': raw.get('rof', '1'), 'Range': raw.get('range', '0'), 'Speed': raw.get('speed', '40'),
               'Burst': raw.get('burst', '1'), 'Report': raw.get('report', ''),
               'LaserDuration': raw.get('laserduration', '15'), 'AmbientDamage': raw.get('ambientdamage', '0'),
               'LaserColor': str(color[0] << 16 | color[1] << 8 | color[2]),
               'Suicide': str(int(yes(raw.get('suicide', 'no')))), 'RadLevel': raw.get('radlevel', '0')}
        for field, key in [('Laser', 'islaser'), ('HouseColor', 'ishousecolor'), ('Sonic', 'issonic'),
                            ('Electric', 'iselectricbolt'), ('RadBeam', 'isradbeam')]:
            row[field] = str(int(yes(raw.get(key, 'no'))))
        ini[f'NativeWeapon.{index}'] = row

