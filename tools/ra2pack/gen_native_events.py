"""Preserve the authored trigger graph; never replace it with generated waves."""


def export_native_events(ini,scenario,rules):
    sections=('triggers','events','actions','tags','celltags','waypoints','scripttypes','taskforces','teamtypes')
    for name in sections:
        ini['NativeEvent.'+name]=dict(scenario.get(name,{}))
    countries=[]
    for table in (rules.get('countries',{}),scenario.get('countries',{})):
        for _,name in sorted(table.items(),key=lambda pair:int(pair[0])):
            if name.lower() not in [value.lower() for value in countries]:countries.append(name)
    names={};numbers={}
    for index,name in enumerate(scenario['houses'].values()):
        country=scenario[name.lower()]['country'];names[name]=str(index);names[country]=str(index)
        numbers[str(next(i for i,v in enumerate(countries) if v.lower()==country.lower()))]=str(index)
    ini['NativeEvent.houses']=names;ini['NativeEvent.houseNumbers']=numbers
    for source,target in (('buildingtypes','buildingTypes'),('vehicletypes','vehicleTypes'),
                          ('infantrytypes','infantryTypes'),('aircrafttypes','aircraftTypes')):
        names=[]
        for table in (rules.get(source,{}),scenario.get(source,{})):
            for _,name in sorted(table.items(),key=lambda pair:int(pair[0])):
                if name.upper() not in names:names.append(name.upper())
        ini['NativeEvent.'+target]={str(i):name for i,name in enumerate(names)}
    ini['NativeEvent.variableNames']=dict(scenario.get('variablenames',{}))
    for table in ('scripttypes','taskforces','teamtypes'):
        for identifier in scenario.get(table,{}).values():
            ini['NativeEvent.record.'+identifier.lower()]=dict(scenario[identifier.lower()])
    export_native_demolition(ini, scenario, rules)
    ini['NativeEvents']={'Version':'4','Triggers':str(len(scenario.get('triggers',{}))),
                         'Tags':str(len(scenario.get('tags',{}))),
                         'StrayDistance':scenario.get('general',{}).get('straydistance',rules.get('general',{}).get('straydistance','2')),
                         'PlayerHouse':str(next(i for i,name in enumerate(scenario['houses'].values()) if scenario[name.lower()].get('playercontrol','no').lower() in ('yes','true','1'))),
                         'FogOfWar':str(int(scenario.get('specialflags',{}).get('fogofwar','yes').lower() in ('yes','true','1'))),
                         'RevealRadius':scenario.get('general',{}).get('revealtriggerradius',rules.get('general',{}).get('revealtriggerradius','9'))}


def export_native_demolition(ini, scenario, rules):
    combat={**rules.get('combatdamage',{}),**scenario.get('combatdamage',{})}
    general={**rules.get('general',{}),**scenario.get('general',{})}
    name=combat['c4warhead'];warhead={**rules.get(name.lower(),{}),**scenario.get(name.lower(),{})}
    ini['NativeEvent.Demolition']={'Warhead':name,'BridgeStrength':combat['bridgestrength'],
        'FlightLevel':general.get('flightlevel','1500'),'CellSpread':warhead.get('cellspread','0'),
        'CellSpreadDefault':str(int('cellspread' not in warhead)),
        'PercentAtMax':warhead.get('percentatmax','1'),'PercentAtMaxDefault':str(int('percentatmax' not in warhead)),
        'Verses':warhead.get('verses',','.join(['100%']*11)),'AnimList':warhead.get('animlist','')}
    for key in ['Wall','Wood','Tiberium']:
        ini['NativeEvent.Demolition'][key]=str(int(warhead.get(key.lower(),'no').lower() in ('yes','true','1')))
