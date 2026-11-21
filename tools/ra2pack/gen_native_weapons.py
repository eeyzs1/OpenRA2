"""Export exact conventional weapon slots; keep spawners and occupant guns distinct."""


def export_native_weapons(ini,rules,scenario,staged_gatling=False):
    merge=lambda name:{**rules.get(name.lower(),{}),**scenario.get(name.lower(),{})}
    yes=lambda text:str(text).lower() in ('yes','true','1')
    general=merge('general')
    def abilities(raw):
        names={v.strip().upper() for v in raw.split(',')}
        return sum(bit for name,bit in [('FIREPOWER',1),('ROF',2),('STRONGER',4)] if name in names)
    for section in list(ini.sections()):
        if not section.startswith('Type.') or ini[section].get('Kind','0')!='0':continue
        row=ini[section];d=merge(row['Id'])
        charge_turret=yes(d.get('ischargeturret')) and d.get('weaponcount')=='1'
        primary=merge(d.get('weapon1' if charge_turret else 'primary',''))
        mode=2 if yes(primary.get('spawner')) else 1 if yes(d.get('canbeoccupied')) else 0
        staged=staged_gatling and yes(d.get('isgattling'))
        selector=4 if staged else 1 if yes(d.get('deployfire')) else 2 if yes(d.get('c4')) else 3 if row['Id'] in ('ADOG','DOG') else 0
        row.update(NativeGuns='1',GunMode=str(mode),GunSelector=str(selector),
                   GunVeteranCombat=general.get('veterancombat','1'),GunVeteranROF=general.get('veteranrof','1'),
                   GunVeteranArmor=general.get('veteranarmor','1'),
                   GunVeteranAbilities=str(abilities(d.get('veteranabilities',''))),
                   GunEliteAbilities=str(abilities(d.get('eliteabilities',''))))
        keys=('weapon1','secondary','eliteweapon1','elitesecondary') if charge_turret else ('primary','secondary','eliteprimary','elitesecondary')
        if staged:
            if d.get('weaponcount')!='6' or d.get('weaponstages')!='3':raise ValueError((row['Id'],'unsupported gatling layout'))
            row.update(GunStages='3',GunRateUp=d['rateup'],GunRateDown=d['ratedown'])
            for stage in range(1,4):
                row['GunStage'+str(stage)]=d['stage'+str(stage)]
                row['GunEliteStage'+str(stage)]=d['elitestage'+str(stage)]
            keys=tuple(key for stage in range(3) for key in (f'weapon{stage*2+1}',f'weapon{stage*2+2}',f'eliteweapon{stage*2+1}',f'eliteweapon{stage*2+2}'))
        for i,key in enumerate(keys):
            name=d.get(key,'');p=f'Gun{i}';present=name.lower() not in ('','none','<none>');row[p+'Present']=str(int(present))
            if not present:continue
            weapon=merge(name);projectile=merge(weapon['projectile']);warhead=merge(weapon['warhead'])
            style=3 if yes(weapon.get('iselectricbolt')) else 0 if yes(projectile.get('inviso')) else 2 if int(projectile.get('rot','0'))>0 else 1
            delay=int(d.get('burstdelay0','-1'))
            row.update({p+'Id':name,p+'Damage':weapon.get('damage','0'),p+'ROF':weapon.get('rof','1'),
                        p+'Range':weapon.get('range','0'),p+'Burst':weapon.get('burst','1'),
                        p+'DelayMin':str(delay if delay>=0 else 3),p+'DelayMax':str(delay if delay>=0 else 5),
                        p+'AA':str(int(yes(projectile.get('aa','no')))),p+'AG':str(int(yes(projectile.get('ag','yes')))),
                        p+'Speed':weapon.get('speed','40'),p+'Style':str(style),p+'Projectile':weapon['projectile'],
                        p+'Report':weapon.get('report',''),p+'Spread':warhead.get('cellspread','0'),
                        p+'PercentAtMax':warhead.get('percentatmax','1'),p+'Verses':warhead.get('verses',','.join(['100%']*11))})
