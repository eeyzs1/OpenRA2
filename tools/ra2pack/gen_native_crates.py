"""Preserve the authored powerup kinds and their map-local rule values."""


def export_native_crates(ini, scenario, rules):
    merged = lambda section: {**rules.get(section, {}), **scenario.get(section, {})}
    crate = merged('craterules')
    ini['NativeCrates'] = {'Version': '1', 'Radius': crate['crateradius'],
                           'SoloMoney': crate['solocratemoney']}
    names = ('Money', 'Unit', 'HealBase', 'Cloak', 'Explosion', 'Napalm', 'Squad', 'Darkness',
             'Reveal', 'Armor', 'Speed', 'Firepower', 'ICBM', 'Invulnerability', 'Veteran',
             'IonStorm', 'Gas', 'Tiberium', 'Pod')
    powerups = merged('powerups')
    ini['NativeCrates.Powerups'] = {name: powerups[name.lower()] for name in names}
