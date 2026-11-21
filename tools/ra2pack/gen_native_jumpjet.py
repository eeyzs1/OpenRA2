"""Retain the original Jumpjet parameters, including flying infantry."""
from gen_native_movement import effective,truth

LOCOMOTOR='{92612c46-f71f-11d1-ac9f-006008055bb5}'

def export_native_jumpjet(ini,scenario,rules):
    controls={**rules.get('jumpjetcontrols',{}),**scenario.get('jumpjetcontrols',{})}
    infantry={value.upper() for value in rules.get('infantrytypes',{}).values()}
    for section in ini:
        if not section.startswith('Type.'):continue
        row=ini[section]
        if row.get('OriginalLocomotor','').lower()!=LOCOMOTOR:continue
        original={**rules.get(row['Id'].lower(),{}),**scenario.get(row['Id'].lower(),{})}
        # The identified base editions specify Crash on every supported type.
        if 'jumpjetcrash' not in original:raise ValueError((row['Id'],'missing verified JumpjetCrash'))
        defaults={'jumpjetspeed':controls.get('speed','14'),'jumpjetturnrate':controls.get('turnrate','4'),
                  'jumpjetclimb':controls.get('climb','5'),'jumpjetheight':controls.get('cruiseheight','500'),
                  'jumpjetaccel':controls.get('acceleration','2'),'jumpjetwobbles':controls.get('wobblespersecond','.15'),
                  'jumpjetdeviation':controls.get('wobbledeviation','40')}
        values={**defaults,**original}
        row.update(SourceAir='1',JumpjetVersion='2',JumpjetSpeed=values['jumpjetspeed'],JumpjetTurnRate=values['jumpjetturnrate'],
                   JumpjetHeight=values['jumpjetheight'],JumpjetDeviation=values['jumpjetdeviation'],
                   JumpjetClimb=effective(values,'jumpjetclimb',5,True),JumpjetCrash=effective(values,'jumpjetcrash',5,True),
                   JumpjetAcceleration=effective(values,'jumpjetaccel',2,True),JumpjetWobbles=effective(values,'jumpjetwobbles',.15,True),
                   JumpjetNoWobbles=str(int(truth(original.get('jumpjetnowobbles')))),
                   JumpjetBalloon=str(int(truth(original.get('balloonhover')))),JumpjetInfantry=str(int(row['Id'] in infantry)))
