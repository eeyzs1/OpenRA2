"""Export original missile-spawner data, including map-local warhead overrides."""


def export_native_rockets(ini,types,rules,scenario,art,include_yr=False,legacy_patriot=True):
    merged=lambda name:{**rules.get(name.lower(),{}),**scenario.get(name.lower(),{})}
    general=merged('general')
    profiles=[('DRED','dmisl','DMISLWH','DMISLEWH'),('V3','v3rocket','V3WH','V3EWH')]
    if include_yr:
        profiles.append(('BSUB','cmisl','CMISLWH','CMISLEWH'))
    ini['NativeRockets']={'Version':'2','Count':str(len(profiles))}
    for index,(launcher,prefix,normal,elite) in enumerate(profiles):
        launch=merged(launcher)
        slot=next((i for i,key in enumerate(('primary','secondary')) if merged(launch.get(key,'' )).get('spawner','no').lower() in ('yes','true','1')),None)
        if slot is None:raise ValueError('original missile spawner weapon missing: '+launcher)
        weapon=merged(launch[('primary','secondary')[slot]])
        missile=general[prefix+'type'].upper();aircraft=merged(missile)
        if launch['spawns'].upper()!=missile:raise ValueError('missile generator type mismatch: '+launcher)
        fields={'Launcher':str(types.index(launcher)),'Missile':str(types.index(missile)),'WeaponSlot':str(slot),
            'Capacity':launch['spawnsnumber'],'Regenerate':launch['spawnregenrate'],
            'Reload':launch['spawnreloadrate'],'LaunchDelay':art.get(missile.lower(),{}).get('spawndelay','0'),
            'ROF':weapon['rof'],'Range':weapon['range'],'MinimumRange':weapon.get('minimumrange','0'),
            'MaxSpeed':aircraft['speed'],'YawRate':aircraft['rot'],'FLH':art[launcher.lower()]['primaryfireflh'],
            'NoSpawnAlt':str(int(launch.get('nospawnalt','no').lower() in ('yes','true','1')))}
        for field,key in [('Pause','pauseframes'),('Tilt','tiltframes'),('Altitude','altitude'),('Damage','damage'),
                      ('EliteDamage','elitedamage'),('BodyLength','bodylength'),('PitchInitial','pitchinitial'),
                      ('PitchFinal','pitchfinal'),('TurnRate','turnrate'),('Acceleration','acceleration')]:
            fields[field]=general[prefix+key]
        fields['LazyCurve']=str(int(general.get(prefix+'lazycurve','no').lower() in ('yes','true','1')))
        section='NativeRocket.'+str(index);ini[section]=fields
        for label,name in [('Normal',normal),('Elite',elite)]:
            warhead=merged(name)
            ini[section+'.'+label]={'Id':name,'CellSpread':warhead['cellspread'],
                'PercentAtMax':warhead['percentatmax'],'Verses':warhead['verses']}
    # The original Patriot must fire real anti-air projectiles at DMISL actors.
    if not legacy_patriot:return
    row=ini['Type.'+str(types.index('NASAM'))];gun=merged(row['Primary']);projectile=merged(gun['projectile']);warhead=merged(gun['warhead'])
    row.update(NativeWeapon='1',WeaponDamage=gun['damage'],WeaponRange=gun['range'],WeaponROF=gun['rof'],
               WeaponProjectile=gun['projectile'],WeaponSpeed=gun['speed'],WeaponReport=gun.get('report',''),
               WeaponAA=str(int(projectile.get('aa','no').lower() in ('yes','true','1'))),
               WeaponAG=str(int(projectile.get('ag','yes').lower() in ('yes','true','1'))),
               WeaponSpread=warhead.get('cellspread','0'),WeaponPercentAtMax=warhead.get('percentatmax','1'),
               WeaponVerses=warhead['verses'])
