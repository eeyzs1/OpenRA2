"""Retain source normal ground locomotion parameters, not prototype delays."""
import math
from fractions import Fraction
import struct

LANDS = ('Clear', 'Road', 'Water', 'Rock', 'Wall', 'Tiberium', 'Beach',
         'Rough', 'Ice', 'Railroad', 'Tunnel', 'Weeds')
SPEEDS = ('Foot', 'Track', 'Wheel', 'Float', 'Winged', 'Hover',
          'Amphibious', 'Creep', 'FloatBeach')
LOCOMOTORS = {
    '{4a582741-9839-11d1-b709-00a024ddafd1}': 'Drive',
    '{4a582742-9839-11d1-b709-00a024ddafd1}': 'Hover',
    '{4a582747-9839-11d1-b709-00a024ddafd1}': 'Teleport',
    '{2bea74e1-7cca-11d3-be14-00104b62a16c}': 'Ship',
}


def original_float(value):
    text = str(value).strip()
    # Original Get_Float uses sscanf("%f"), then promotes that float to double.
    result = struct.unpack('<f',struct.pack('<f',float(text.rstrip('%'))))[0]
    if not math.isfinite(result) or result < 0:
        raise ValueError(('invalid source movement number', value))
    if text.endswith('%'):
        exact=Fraction.from_float(result)*Fraction.from_float(.01)
        result=float(exact)
        if Fraction.from_float(result)>exact:result=math.nextafter(result,0)
    return result


def effective(source,key,default,stored_float=False):
    result=original_float(source[key]) if key in source else float(default)
    if stored_float:
        single=struct.unpack('<f',struct.pack('<f',result))[0]
        if single>result:
            bits=struct.unpack('<I',struct.pack('<f',single))[0]-1
            single=struct.unpack('<f',struct.pack('<I',bits))[0]
        result=single
    return format(result,'.17g')


def truth(value, default=False):
    return str(value).lower() in ('yes', 'true', '1') if value is not None else default


def export_native_movement(ini, scenario, rules, allow_air=False):
    general = {**rules.get('general', {}), **scenario.get('general', {})}
    visual = {**rules.get('audiovisual', {}), **scenario.get('audiovisual', {})}
    ini['NativeMovement'] = {
        'Version': '1', 'Lands': str(len(LANDS)),
        'TrackedUphill': effective(general,'trackeduphill',1),
        'TrackedDownhill': effective(general,'trackeddownhill',1),
        'WheeledUphill': effective(general,'wheeleduphill',1),
        'WheeledDownhill': effective(general,'wheeleddownhill',1),
        'ConditionYellow': effective(visual,'conditionyellow',.5),
        'HoverAcceleration': effective(general,'hoveracceleration',.02),
        'HoverBrake': effective(general,'hoverbrake',.03),
        'HoverBoost': effective(general,'hoverboost',1.5),
    }
    for i, name in enumerate(LANDS):
        source = {**rules.get(name.lower(), {}), **scenario.get(name.lower(), {})}
        ini[f'NativeMovement.Land.{i}'] = {
            'Name': name, **{speed: effective(source,speed.lower(),0,True)
                             for speed in SPEEDS if speed != 'Winged'}}
    for i, name in enumerate(scenario['houses'].values()):
        country_name = scenario[name.lower()]['country'].lower()
        country = {**rules.get(country_name, {}), **scenario.get(country_name, {})}
        ini[f'NativeMovement.House.{i}'] = {'SpeedUnitsMult': effective(country,'speedunitsmult',1,True)}
    infantry = {name.upper() for name in rules.get('infantrytypes', {}).values()}
    aircraft = {name.upper() for name in rules.get('aircrafttypes', {}).values()}
    for section in list(ini):
        if not section.startswith('Type.'):
            continue
        row = ini[section]
        if row['Building'] != '0' or row['Id'] in infantry or row.get('NativeRocket') == '1':
            continue
        original = {**rules.get(row['Id'].lower(), {}), **scenario.get(row['Id'].lower(), {})}
        if allow_air and (row['Id'] in aircraft or truth(original.get('balloonhover')) or original.get('locomotor','').lower()
                          == '{92612c46-f71f-11d1-ac9f-006008055bb5}'):
            row['SourceAir']='1'
            if original.get('locomotor','').lower()=='{4a582746-9839-11d1-b709-00a024ddafd1}':
                row.update(FlightVersion='1',FlightROT=original.get('rot','0'),
                           FlightSlowdownDistance=original.get('slowdowndistance','500'))
            continue
        loco = row['OriginalLocomotor'].lower()
        if loco not in LOCOMOTORS:
            raise ValueError((row['Id'], 'unsupported original ground locomotor', loco))
        default_speed = 'Track' if truth(original.get('crusher')) else 'Wheel'
        speed = original.get('speedtype', default_speed)
        speed = next((s for s in SPEEDS if s.lower() == speed.lower()), None)
        if speed is None:
            raise ValueError((row['Id'], 'unknown original SpeedType'))
        row.update(MovementLocomotor=LOCOMOTORS[loco], OriginalSpeedType=speed,
                   OriginalAccelerates=str(int(truth(original.get('accelerates'), True))),
                   OriginalPassive=str(int(truth(original.get('passive')))),
                   OriginalAccelerationFactor=effective(original,'accelerationfactor',.03),
                   OriginalDeaccelerationFactor=effective(original,'deaccelerationfactor',.002),
                   MovementVeteranSpeed=effective(general,'veteranspeed',1),
                   OriginalSlowdownDistance=original.get('slowdowndistance', '500'))
    return ini['NativeMovement']
