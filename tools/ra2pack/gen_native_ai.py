"""Keep campaign AI triggers and recruitment permissions from the original map."""


def export_native_ai(ini, scenario, rules):
    general = {**rules.get('general', {}), **scenario.get('general', {})}
    fields = {'Version': '1', 'IgnoreGlobalAITriggers': str(int(scenario['basic'].get('ignoreglobalaitriggers', 'no').lower() == 'yes'))}
    for key in ('TeamDelays', 'TotalAITeamCap', 'MinimumAIDefensiveTeams', 'MaximumAIDefensiveTeams', 'FillEarliestTeamProbability'):
        fields[key] = general[key.lower()]
    fields['UseMinDefenseRule'] = str(int(general.get('usemindefenserule', 'yes').lower() == 'yes'))
    for key, fallback in [('AITriggerSuccessWeightDelta', '1'), ('AITriggerFailureWeightDelta', '-1'), ('AITriggerTrackRecordCoefficient', '1')]:
        fields[key] = general.get(key.lower(), fallback)
    ini['NativeAI'] = fields
    ini['NativeAI.Triggers'] = dict(scenario.get('aitriggertypes', {}))
    ini['NativeAI.Enabled'] = dict(scenario.get('aitriggertypesenable', {}))
    for i, name in enumerate(scenario['houses'].values()):
        house = scenario[name.lower()]
        ini['NativeAI.House.' + str(i)] = {'Country': house['country'], 'RatioAITriggerTeam': house.get('ratioaitriggerteam', '100')}
    for section in ini.sections():
        if not section.startswith('Actor.'):
            continue
        actor = ini[section]
        if actor['Key'].startswith(('infantry:', 'units:')):
            original = actor['Original'].split(',')
            infantry = actor['Key'].startswith('infantry:')
            # Placed-object records end in the two independent recruit flags.
            ini['NativeAI.Recruit.' + section[6:]] = {'Object': actor['Key'],
                'Recruitable': original[-2], 'AutocreateRecruitable': original[-1],
                'Group': original[10 if infantry else 9]}
