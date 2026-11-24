"""Preserve native production identity and map-local eligibility, not prototype tech."""
from decimal import Decimal
import hashlib
from PIL import Image
from ra2lib import Shp,load_pal,shp_frame_to_rgba


SOURCE_SW_TYPES = ('MultiMissile', 'LightningStorm', 'IronCurtain', 'ChronoSphere',
                   'GeneticConverter', 'PsychicDominator', 'ForceShield')
SOURCE_SUPPORT_TYPES = ('ParaDrop', 'AmerParaDrop', 'SpyPlane', 'PsychicReveal')


def original_building_cost(definition, rules, identity):
    """BuildingType cost after FreeUnit and SeparateAircraft adjustments."""
    general = rules['general']
    cost = int(definition.get('cost', '0'))
    if general.get('separateaircraft', 'no').lower() not in ('yes', 'true', '1'):
        aircraft = [rules[name.strip().lower()] for name in general['padaircraft'].split(',')]
        if identity.lower() == aircraft[0].get('dock', '').split(',')[0].strip().lower():
            cost -= (int(aircraft[0].get('cost', '0')) + int(aircraft[1].get('cost', '0'))) // 2
    free = rules.get(definition.get('freeunit', '').lower(), {})
    return max(0, cost - int(free.get('cost', '0')))


def building_repair_cost(definition, rules, identity):
    """Original BuildingType cost and x87-truncated TechnoType repair charge.

    Binary64 RepairPercent is multiplied while retained in the x87 register:
    20 * binary64(.15) remains below3. Rounding the product to a Python float
    before truncation would incorrectly export3 instead of the original2.
    """
    general = rules['general']
    step = int(general.get('repairstep', '8'))
    if step <= 0:
        raise ValueError(('unsupported original building repair step', identity))
    count = int(definition.get('strength', '1')) // step
    if count <= 0:
        raise ValueError(('unsupported original building repair divisor', identity))
    cost = original_building_cost(definition, rules, identity)
    fraction = general.get('repairpercent', '15%')
    fraction = float(fraction[:-1]) / 100 if fraction.endswith('%') else float(fraction)
    return max(1, int(Decimal(cost // count) * Decimal.from_float(fraction)))


def building_sale_values(definition, rules, identity):
    """Local source houses have unit building/defense cost multipliers.

    Original nonzero Soylent overrides both policies. The ordinary human
    predicate applies RefundPercent; other ordinary houses receive type cost.
    The original steady x87 control word truncates the single-precision rate.
    """
    import struct
    soylent = int(definition.get('soylent', '0'))
    if soylent:
        if soylent < 0: raise ValueError(('negative source building Soylent', identity))
        return soylent, soylent
    cost = original_building_cost(definition, rules, identity)
    raw = rules['general'].get('refundpercent', '50%')
    rate = float(raw[:-1]) / 100 if raw.endswith('%') else float(raw)
    if not 0 <= rate <= 1: raise ValueError(('unsupported source RefundPercent', raw))
    bits = struct.unpack('<I', struct.pack('<f', rate))[0]
    single = struct.unpack('<f', struct.pack('<I', bits))[0]
    if single > rate: single = struct.unpack('<f', struct.pack('<I', bits-1))[0]
    return int(Decimal(cost) * Decimal.from_float(single)), cost


def production_economy(definition, rules, identity='', edition='ra2'):
    yes = lambda key: definition.get(key, 'no').lower() in ('yes', 'true', '1')
    fields = {'RequiredStolenTech': str(sum(bit for bit, key in (
        (1, 'requiresstolenalliedtech'), (2, 'requiresstolensoviettech'),
        (4, 'requiresstolenthirdtech')) if yes(key))),
        'FactoryPlant': str(int(yes('factoryplant'))), 'Powered': str(int(yes('powered')))}
    weapon_mask = support_mask = 0
    for key in ('superweapon', 'superweapon2'):
        ability = rules.get(definition.get(key, '').lower(), {}).get('type', '').lower()
        for i, kind in enumerate(SOURCE_SW_TYPES):
            if ability == kind.lower():
                weapon_mask |= 1 << i
        for i, kind in enumerate(SOURCE_SUPPORT_TYPES):
            if ability == kind.lower(): support_mask |= 1 << i
    fields.update(SuperweaponMask=str(weapon_mask), SupportMask=str(support_mask))
    for key in ('InfantryCostBonus', 'UnitsCostBonus', 'AircraftCostBonus', 'BuildingsCostBonus', 'DefensesCostBonus'):
        fields[key] = definition.get(key.lower(), '1')
    for key in ('Grinding', 'InfantryAbsorb', 'UnitAbsorb', 'Bunker', 'Cloning', 'Spyable'):
        fields[key] = str(int(yes(key.lower())))
    fields.update(Soylent=definition.get('soylent', '-1'), Passengers=definition.get('passengers', '0'),
                  SizeLimit=definition.get('sizelimit', '0'), ExtraPower=definition.get('extrapower', '0'),
                  Cloneable=str(int(definition.get('cloneable', 'yes').lower() in ('yes', 'true', '1'))))
    building = identity.upper() in {value.upper() for value in rules.get('buildingtypes', {}).values()}
    tech = [value.strip().upper() for value in rules.get('ai', {}).get('buildtech', '').split(',')]
    # The original lists start NATECH,GATECH: their position is not an Allied/
    # Soviet technology bit. YR's original laboratory identities use their side;
    # RA2 resolves the victim house's side when infiltration actually occurs.
    tech_side = int(definition.get('aibaseplanningside', '-1'))
    tech_mask = (1 << tech_side) if identity.upper() in tech and 0 <= tech_side < 3 else 0
    factory = definition.get('factory', '').lower()
    # Preserve one vanilla effect for the identified original building roles.
    # Ares custom SpyEffect.* extensions require a separate explicit contract.
    effect = 0
    if building:
        if yes('radar'): effect = 3
        elif int(definition.get('power', '0')) > 0: effect = 2
        # YR laboratories grant technology despite their ForceShieldSpecial.
        # RA2's original routine checks primary SuperWeapon before BuildTech
        # (GAME.EXE 0x452390); keep that edition's precedence separate.
        elif edition == 'yr' and tech_mask: effect = 6
        elif definition.get('superweapon', '').lower() in rules: effect = 7
        elif tech_mask: effect = 6
        elif int(definition.get('storage', '0')) > 0: effect = 1
        elif factory == 'unittype': effect = 5
        elif factory == 'infantrytype': effect = 4
    fields.update(SpyEffect=str(effect), SpyTechMask=str(tech_mask if effect == 6 else 0))
    primary = rules.get(definition.get('superweapon', '').lower(), {}).get('type', '').lower()
    fields.update(SpyResetWeapons=str(sum(1 << i for i, kind in enumerate(SOURCE_SW_TYPES) if primary == kind.lower())),
                  SpyResetSupport=str(sum(1 << i for i, kind in enumerate(SOURCE_SUPPORT_TYPES) if primary == kind.lower())))
    return fields


def export_support_rules(ini, rules, edition, catalog_id, output, get):
    folder = output.parent / catalog_id / 'support';folder.mkdir(parents=True, exist_ok=True)
    general = rules['general']
    camera = rules.get('spycameraweapon', {})
    ini['NativeProduction.Delivery'] = {'FlightLevel': general['flightlevel'],
        'ParachuteMaxFallRate': general['parachutemaxfallrate'], 'ParadropRadius': general['paradropradius'],
        'SpyRevealRadius': camera.get('damage', '6'), 'SpyCameraRange': camera.get('range', '20')}
    for index, kind in enumerate(SOURCE_SUPPORT_TYPES):
        candidates = [(name, rules.get(name.lower(), {})) for name in rules['superweapontypes'].values()
                      if rules.get(name.lower(), {}).get('type', '').lower() == kind.lower()]
        if len(candidates) > 1: raise ValueError(('duplicate original support type', kind))
        definition = candidates[0][1] if candidates else {}
        fields = {'Available': str(int(bool(candidates))), 'RechargeTicks': str(2 * int(Decimal(definition['rechargetime']) * 900)) if candidates else '0',
                  'IsPowered': str(int(definition.get('ispowered', 'no').lower() in ('yes', 'true', '1'))), 'Icon': ''}
        if candidates:
            name = definition['sidebarimage'].lower() + '.shp';source, raw = get(name);_, pal = get('cameo.pal')
            if raw is None or pal is None: raise FileNotFoundError(('original support cameo', edition, name))
            shp = Shp(raw);frame = shp.frame_pixels(0);image = Image.new('RGBA', (shp.w, shp.h))
            if frame.w and frame.h: image.alpha_composite(shp_frame_to_rgba(frame, load_pal(pal), remap=False), (frame.x, frame.y))
            icon = folder / (kind.lower() + '.png');image.save(icon)
            fields.update(Icon='assets/rules/profiles/' + catalog_id + '/support/' + icon.name,
                          IconSource=source, CameoSHA256=hashlib.sha256(raw).hexdigest(), IconSHA256=hashlib.sha256(icon.read_bytes()).hexdigest())
        for side, prefix in [('Allied', 'ally'), ('Soviet', 'sov'), ('Yuri', 'yuri')]:
            payload = bool(candidates) and index < 2 and (edition == 'yr' or side != 'Yuri')
            key = 'amer' if index == 1 else prefix
            fields[side + 'Infantry'] = general[key + 'paradropinf'].upper() if payload else ''
            fields[side + 'Count'] = general[key + 'paradropnum'] if payload else '0'
        ini['NativeProduction.Support.' + str(index)] = fields


def production_timing(definition, general):
    frames = int(Decimal(definition.get('cost','0')) * Decimal(general['buildspeed'])
                 * Decimal('.9') * Decimal(definition.get('buildtimemultiplier','1')))
    return frames, 2 * max(1, min(255, frames // 54))


def export_native_production(ini,scenario,rules,art,target,get,tree):
    merged=lambda key:{**rules.get(key.lower(),{}),**scenario.get(key.lower(),{})}
    general=merged('general')
    ini['NativeProduction']={'Version':'1','BuildSpeed':general['buildspeed'],
        'MultipleFactory':general.get('multiplefactory','1'),
        'MinLowPowerProductionSpeed':general.get('minlowpowerproductionspeed','.5'),
        'MaxLowPowerProductionSpeed':general.get('maxlowpowerproductionspeed','.8'),
        'LowPowerPenaltyModifier':general.get('lowpowerpenaltymodifier','1'),
        'MaximumQueuedObjects':general.get('maximumqueuedobjects','29')}
    ini['NativeProduction.Prerequisites']={key[12:].upper():value for key,value in general.items() if key.startswith('prerequisite')}
    for i,name in enumerate(scenario['houses'].values()):
        house=scenario[name.lower()];country=house['country'];seen=set()
        while country.lower() not in seen:
            seen.add(country.lower());parent=merged(country).get('parentcountry')
            if not parent or parent.lower() in ('none',country.lower()):break
            country=parent
        ini['NativeProduction.House.'+str(i)]={'Country':country,'TechLevel':house.get('techlevel','1')}
    tables={name:{v.upper() for table in (rules.get(name,{}),scenario.get(name,{})) for v in table.values()} for name in ('buildingtypes','infantrytypes','vehicletypes','aircrafttypes')}
    for section in [s for s in ini.sections() if s.startswith('Type.')]:
        row=ini[section]
        if int(row.get('Kind','0')):continue
        id=row['Id'];d=merged(id);a=merged(id).get('image',id).lower();visual=art.get(a,{})
        building=row['Building']=='1';infantry=id in tables['infantrytypes'];air=id in tables['aircrafttypes']
        naval=d.get('naval','no').lower() in ('yes','true','1')
        category=(5 if d.get('isbasedefense','no').lower() in ('yes','true','1') else 4) if building else 0 if infantry else 2 if air else 3 if naval else 1
        frames,step_ticks=production_timing(d,general)
        fields={'TechLevel':d.get('techlevel','-1'),'Category':str(category),'Factory':d.get('factory','none'),
            'Owner':d.get('owner',''),'RequiredHouses':d.get('requiredhouses',''),'ForbiddenHouses':d.get('forbiddenhouses',''),
            'Prerequisite':d.get('prerequisite',''),'BuildLimit':d.get('buildlimit','2147483647'),
            'BaseBuildFrames':str(frames),'StepTicks':str(step_ticks),
            'BaseNormal':str(int(d.get('basenormal','yes').lower() in ('yes','true','1'))),
            'Naval':str(int(naval)),
            'Adjacent':d.get('adjacent','2'),'FreeUnit':d.get('freeunit',''),'Cameo':visual.get('cameo','')}
        table='buildingtypes' if building else 'infantrytypes' if infantry else 'aircrafttypes' if air else 'vehicletypes'
        fields['Order']=str(next((i for i,(_,name) in enumerate(sorted(merged(table).items(),key=lambda item:int(item[0]))) if name.upper()==id),4095))
        fields['InfantryExit']=str(1 if d.get('gdibarracks','no').lower()=='yes' else 2 if d.get('nodbarracks','no').lower()=='yes' else 0)
        ini['NativeProduction.Type.'+section[5:]]=fields
        if fields['Cameo'] and int(fields['TechLevel'])>=0:
            copies=[(p,b) for p,b in tree.find_all(fields['Cameo'].lower()+'.shp') if p.lower().startswith(('language.mix/','ra2.mix/'))]
            if not copies:raise FileNotFoundError('original production cameo: '+id)
            path,raw=sorted(copies,key=lambda p:p[0].lower())[0];_,pal=get('cameo.pal')
            if pal is None:raise FileNotFoundError('original cameo palette')
            ini['NativeProduction.Type.'+section[5:]]['CameoSource']=path
            ini['NativeProduction.Type.'+section[5:]]['CameoSHA256']=hashlib.sha256(raw).hexdigest()
            shp=Shp(raw);frame=shp.frame_pixels(0);image=Image.new('RGBA',(shp.w,shp.h))
            if frame.w and frame.h:image.alpha_composite(shp_frame_to_rgba(frame,load_pal(pal),remap=False),(frame.x,frame.y))
            image.save(target/'actors'/(id.lower()+'_icon.png'))
