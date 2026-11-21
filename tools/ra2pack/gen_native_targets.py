"""Preserve source target categories, primary/secondary verses and rating coefficients."""
from decimal import Decimal

COEFFICIENTS = (
    ('TargetMyEffectiveness','myeffectivenesscoefficient'),
    ('TargetEffectiveness','targeteffectivenesscoefficient'),
    ('TargetSpecialCoefficient','targetspecialthreatcoefficient'),
    ('TargetStrength','targetstrengthcoefficient'),
    ('TargetDistance','targetdistancecoefficient'),
)


def export_native_targets(ini,rules,scenario):
    yes=lambda value:str(value).lower() in ('yes','true','1')
    merged=lambda name:{**rules.get(name.lower(),{}),**scenario.get(name.lower(),{})}
    general=merged('general')
    for section in list(ini.sections()):
        if not section.startswith('Type.') or ini[section].get('Kind','0')!='0':continue
        row=ini[section];id=row['Id'].lower();base=rules.get(id,{});local=scenario.get(id,{})
        definition={**base,**local}
        row.update(TargetProfile='1',TargetLegal=str(int(yes(definition.get('legaltarget','yes')))),
                   TargetInvisible=str(int(yes(definition.get('invisibleingame','no')))),
                   TargetStorage=str(int(Decimal(definition.get('storage','0'))!=0)),
                   TargetFactory=str(int(definition.get('factory','none').lower() not in ('','none','<none>'))),
                   TargetUndeploys=str(int(definition.get('undeploysinto','none').lower() not in ('','none','<none>'))),
                   TargetDetectDisguise=str(int(yes(definition.get('detectdisguise','no')))),
                   TargetAIDetectDisguise=str(int(yes(definition.get('aidetectdisguise','no')))),
                   TargetSpecialThreat=definition.get('specialthreatvalue','0'))
        for output,key in COEFFICIENTS:
            default=general.get(key+'default','0');value=local.get(key,base.get(key,default))
            # A zero value falls back when a subsequent rules layer reloads it.
            if id in scenario and Decimal(value)==0:value=default
            row[output]=value
        for slot,key in enumerate(('primary','secondary')):
            name=definition.get(key,'');prefix=f'TargetWeapon{slot}'
            row[prefix+'Present']=str(int(bool(name and name.lower() not in ('none','<none>'))))
            if row[prefix+'Present']=='0':continue
            weapon=merged(name);projectile=merged(weapon['projectile']);warhead=merged(weapon['warhead'])
            row.update({prefix+'Range':weapon.get('range','0'),prefix+'AA':str(int(yes(projectile.get('aa','no')))),
                        prefix+'AG':str(int(yes(projectile.get('ag','yes')))),
                        prefix+'Verses':warhead.get('verses',','.join(['100%']*11)),
                        prefix+'VersesDefault':str(int('verses' not in warhead))})
