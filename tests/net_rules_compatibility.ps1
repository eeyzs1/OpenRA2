param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [int]$Frames=360,
    [int]$TimeoutSeconds=60,
    [string]$Case='',
    [switch]$ProfilesOnly
)
$ErrorActionPreference='Stop'
trap {[Console]::Error.WriteLine($_.Exception.Message);exit 1}
$netRoot=Split-Path -Parent $PSScriptRoot
$netOutput=[IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $netOutput -Force | Out-Null
$netEncoding=[Text.UTF8Encoding]::new($false)
function Write-NetMod([string]$Name,[string]$Rules='', [string]$Projectiles='', [string]$BuildingsCsv='') {
    $netMod=Join-Path $netOutput "fixtures/$Name"
    New-Item -ItemType Directory -Path (Join-Path $netMod 'rules') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $netMod 'mod.ini'),"[Mod]`nEnabled=no`nLoadOrder=2147483647`n",$netEncoding)
    foreach($entry in @(@('rules.ini',$Rules),@('projectiles.ini',$Projectiles),@('buildings.csv',$BuildingsCsv))){
        if($entry[1]){[IO.File]::WriteAllText((Join-Path $netMod ('rules/'+$entry[0])),$entry[1],$netEncoding)}
    }
    return $netMod
}
$netHp=Write-NetMod 'hp-1001' "[Bld.ConYard]`nHP=1001`n"
$netEconomic=Write-NetMod 'ore' "[GameRules]`nOreRegrowRate=17`n"
$netWeapon=Write-NetMod 'weapon' "[Unit.Grizzly]`nWeapon.Damage=999`n"
$netElite=Write-NetMod 'elite' "[Unit.Apocalypse]`nElite.Damage=999`n"
$netAir=Write-NetMod 'air' "[Unit.Apocalypse]`nAirWeapon.Damage=999`n"
$netNonfinite=Write-NetMod 'nonfinite' "[GameRules]`nVeteranRatio=nan`n"
$netDisplay=Write-NetMod 'display' "; labels and sound names only`n[Unit.Grizzly]`nName=Another display name`nWeapon.Report=AnotherReport`n"
$netEquivalentA=Write-NetMod 'equivalent-a' "[Bld.ConYard]`nHP=1001`n[GameRules]`nOreRegrowRate=17`n"
$netEquivalentB=Write-NetMod 'equivalent-b' "; same parsed values in another order`n[GameRules]`nOreRegrowRate = 017`n[Bld.ConYard]`nHP = 1001`n"
$netCsv=Write-NetMod 'csv-hp' '' '' "Name,HP`nConYard,1001`n"
$netShadow=Write-NetMod 'csv-shadow' "[Bld.ConYard]`nHP=1002`n" '' "Name,HP`nConYard,1001`n"
$netBaseProjectiles=[IO.File]::ReadAllText((Join-Path $netRoot 'assets/rules/projectiles.ini'))
$netProjectileText=([regex]'(?m)^ROT\s*=\s*[^\r\n]*').Replace($netBaseProjectiles,'ROT=111',1)
if($netProjectileText -eq $netBaseProjectiles){throw 'Projectile fixture did not change an actual ROT field'}
$netProjectile=Write-NetMod 'projectile' '' $netProjectileText
$netImageText=([regex]'(?m)^Image\s*=\s*[^\r\n]*').Replace($netBaseProjectiles,'Image=AnotherImage',1)
if($netImageText -eq $netBaseProjectiles){throw 'Image fixture did not change an actual Image field'}
$netImage=Write-NetMod 'image' '' $netImageText
$netNamedA=Write-NetMod 'named-psychic' "[Unit.Apocalypse]`nWeapon.Projectile=Psychic`n"
$netNamedB=Write-NetMod 'named-invisible' "[Unit.Apocalypse]`nWeapon.Projectile=Invisible`n"
$netCases=@(
    @{name='control';reason=0},
    @{name='client-hp-1001';client=$netHp;reason=2},
    @{name='host-hp-1001';host=$netHp;reason=2},
    @{name='economy';client=$netEconomic;reason=2},
    @{name='weapon';client=$netWeapon;reason=2},
    @{name='elite';client=$netElite;reason=2},
    @{name='anti-air';client=$netAir;reason=2},
    @{name='projectile';client=$netProjectile;reason=2},
    @{name='named-projectile';host=$netNamedA;client=$netNamedB;reason=2},
    @{name='csv-hp';client=$netCsv;reason=2},
    @{name='same-parsed-values';host=$netEquivalentA;client=$netEquivalentB;reason=0},
    @{name='csv-shadows-ini';host=$netHp;client=$netShadow;reason=0},
    @{name='display-only';client=$netDisplay;reason=0},
    @{name='image-only';client=$netImage;reason=0},
    @{name='nonfinite';client=$netNonfinite;reason=3}
)
if($ProfilesOnly){
    $netProfile=[IO.File]::ReadAllText((Join-Path $netRoot 'assets/rules/profiles/ra2.ini'))
    $netChanged=$netProfile
    if($netProfile.Contains('[NativeWeapons]')){
        function Set-NetSourceField([string]$Text,[string]$Section,[string]$Key,[string]$Value) {
            $netStart=$Text.IndexOf("[$Section]",[StringComparison]::Ordinal)
            if($netStart -lt 0){throw "Missing source section: $Section"}
            $netEnd=$Text.IndexOf("`n[",$netStart+1,[StringComparison]::Ordinal)
            if($netEnd -lt 0){$netEnd=$Text.Length}
            $netBlock=$Text.Substring($netStart,$netEnd-$netStart)
            $netPattern='(?m)^'+[regex]::Escape($Key)+'\s*=\s*[^\r\n]*'
            if([regex]::Matches($netBlock,$netPattern).Count -ne 1){throw "Missing or duplicate source field: $Section/$Key"}
            $netBlock=[regex]::Replace($netBlock,$netPattern,"$Key = $Value")
            return $Text.Substring(0,$netStart)+$netBlock+$Text.Substring($netEnd)
        }
        $netBlocks=[regex]::Matches($netProfile,'(?ms)^\[([^\]]+)\]\r?\n(.*?)(?=^\[|\z)')
        $netM60=$null
        foreach($netBlock in $netBlocks){
            if($netBlock.Groups[1].Value -match '^NativeWeapon\.(\d+)$' -and $netBlock.Groups[2].Value -match '(?m)^Id\s*=\s*M60\s*$'){
                $netM60=$netBlock.Groups[1].Value.Substring('NativeWeapon.'.Length);break
            }
        }
        if($null -eq $netM60){throw 'Source fixture has no original M60 catalog entry'}
        $netChanged=Set-NetSourceField $netChanged "NativeWeapon.$netM60" 'Damage' '137'
        $netMirrors=0
        foreach($netBlock in $netBlocks){if($netBlock.Groups[1].Value -notmatch '^Type\.\d+$'){continue}
            foreach($netReference in [regex]::Matches($netBlock.Groups[2].Value,"(?m)^Gun(\d+)Reference\s*=\s*$netM60\s*$")){
                $netSlot=$netReference.Groups[1].Value
                $netChanged=Set-NetSourceField $netChanged $netBlock.Groups[1].Value "Gun${netSlot}Damage" '137';$netMirrors++
            }
        }
        if($netMirrors -eq 0){throw 'Source fixture has no actual gun using M60'}
    } else {
        $netChanged=([regex]'(?m)^Gun0Damage\s*=\s*15\s*$').Replace($netProfile,'Gun0Damage = 137',1)
    }
    if($netChanged -eq $netProfile){throw 'Source fixture did not change an actual original weapon'}
    $netSourceMod=Write-NetMod 'source-damage'
    $netBrokenMod=Write-NetMod 'source-malformed'
    foreach($netPath in @($netSourceMod,$netBrokenMod)){New-Item -ItemType Directory -Path (Join-Path $netPath 'rules/profiles') -Force | Out-Null}
    [IO.File]::WriteAllText((Join-Path $netSourceMod 'rules/profiles/ra2.ini'),$netChanged,$netEncoding)
    [IO.File]::WriteAllText((Join-Path $netBrokenMod 'rules/profiles/ra2.ini'),"[Scene]`nVersion = 20`n",$netEncoding)
    $netArtMod=Write-NetMod 'source-broken-art'
    $netArtPath=Join-Path $netArtMod 'rules/profiles/ra2-local-base/actors'
    New-Item -ItemType Directory -Path $netArtPath -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $netArtPath 'asw_d0.png'),[byte[]]@(1,2,3,4,5))
    $netCases=@(
        @{name='ra2-control';hostRules='ra2';clientRules='ra2';reason=0},
        @{name='yr-control';hostRules='yr';clientRules='yr';reason=0},
        @{name='yr-yuri';hostRules='yr';clientRules='yr';hostCountry=11;clientCountry=6;reason=0},
        @{name='ra2-random';hostRules='ra2';clientRules='ra2';hostCountry=12;clientCountry=12;reason=0},
        @{name='yr-random';hostRules='yr';clientRules='yr';hostCountry=12;clientCountry=12;reason=0},
        @{name='different-profile';hostRules='ra2';clientRules='yr';reason=5},
        @{name='different-source-damage';hostRules='ra2';clientRules='ra2';client=$netSourceMod;reason=2},
        @{name='same-source-damage';hostRules='ra2';clientRules='ra2';host=$netSourceMod;client=$netSourceMod;reason=0},
        @{name='malformed-source';hostRules='ra2';clientRules='ra2';client=$netBrokenMod;reason=3},
        @{name='unavailable-source-art';hostRules='ra2';clientRules='ra2';client=$netArtMod;reason=3},
        @{name='unsupported-source-country';hostRules='ra2';clientRules='ra2';clientCountry=11;reason=4}
    )
}
if($Case){$netCases=@($netCases | Where-Object {$_.name -eq $Case});if(!$netCases.Count){throw "Unknown case: $Case"}}
$netRecords=@()
foreach($netCase in $netCases){
    $netServer=$null;$netClient=$null
    try {
        $netName=$netCase.name
        $netHostOut=Join-Path $netOutput "$netName-host.log";$netHostErr=Join-Path $netOutput "$netName-host.err"
        $netClientOut=Join-Path $netOutput "$netName-client.log";$netClientErr=Join-Path $netOutput "$netName-client.err"
        $netHostArgs=@('--net-host',"$Frames");if($netCase.host){$netHostArgs+=@('--mod',('"'+$netCase.host+'"'))}
        $netClientArgs=@('--net-client',"$Frames");if($netCase.client){$netClientArgs+=@('--mod',('"'+$netCase.client+'"'))}
        if($netCase.hostRules){$netHostArgs+=@('--ruleset',$netCase.hostRules)}
        if($netCase.clientRules){$netClientArgs+=@('--ruleset',$netCase.clientRules)}
        if($netCase.hostCountry){$netHostArgs+=@('--net-country',"$($netCase.hostCountry)")}
        if($netCase.clientCountry){$netClientArgs+=@('--net-country',"$($netCase.clientCountry)")}
        $netServer=Start-Process -FilePath $Exe -ArgumentList $netHostArgs -WorkingDirectory $netRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $netHostOut -RedirectStandardError $netHostErr
        $null=$netServer.Handle;Start-Sleep -Milliseconds 500
        $netClient=Start-Process -FilePath $Exe -ArgumentList $netClientArgs -WorkingDirectory $netRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $netClientOut -RedirectStandardError $netClientErr
        $null=$netClient.Handle;$netDeadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while((!$netServer.HasExited -or !$netClient.HasExited) -and [DateTime]::UtcNow -lt $netDeadline){Start-Sleep -Milliseconds 100;$netServer.Refresh();$netClient.Refresh()}
        if(!$netServer.HasExited -or !$netClient.HasExited){throw "$netName timed out"}
        $netServer.WaitForExit();$netClient.WaitForExit();$netServer.Refresh();$netClient.Refresh()
        $netH=([IO.File]::ReadAllText($netHostOut))+([IO.File]::ReadAllText($netHostErr));$netC=([IO.File]::ReadAllText($netClientOut))+([IO.File]::ReadAllText($netClientErr))
        if($netCase.reason){
            $netPattern="net-test: rejected reason=$($netCase.reason) tick=0"
            if($netH.Contains('net-test: game begin') -or $netC.Contains('net-test: game begin') -or $netServer.ExitCode -ne 2 -or $netClient.ExitCode -ne 2 -or !$netH.Contains($netPattern) -or !$netC.Contains($netPattern)){throw "$netName did not reject both peers before startup; inspect retained logs"}
        } else {
            $netPattern='net-test: done tick=(\d+) checksum=([0-9a-fA-F]+) desync=0'
            $netA=[regex]::Match($netH,$netPattern);$netB=[regex]::Match($netC,$netPattern)
            if($netServer.ExitCode -ne 0 -or $netClient.ExitCode -ne 0 -or !$netA.Success -or !$netB.Success -or [int]$netA.Groups[1].Value -lt $Frames -or [int]$netB.Groups[1].Value -lt $Frames -or $netA.Groups[2].Value -ne $netB.Groups[2].Value){throw "$netName did not complete an identical simulation; inspect retained logs"}
            if($netCase.hostRules){
                $netExpected=if($netCase.hostRules -eq 'ra2'){1}else{2}
                foreach($netText in @($netH,$netC)){
                    if($netText -notmatch "net-test: game begin player=\d rules=$netExpected source=$($netCase.hostRules)-[^ ]+ countries=(\d+),(\d+)"){throw "$netName did not install the selected source profile"}
                    if([int]$Matches[1] -eq 10 -or [int]$Matches[2] -eq 10 -or ($netExpected -eq 1 -and ([int]$Matches[1] -eq 11 -or [int]$Matches[2] -eq 11))){throw "$netName resolved a country outside the source edition"}
                    if($netCase.name -eq 'yr-yuri' -and ($Matches[1] -ne '11' -or $Matches[2] -ne '6')){throw 'The selected Yuri country was not applied'}
                }
            }
        }
        $netRecords+=@{name=$netName;expected_rejection=$netCase.reason;host_exit=$netServer.ExitCode;client_exit=$netClient.ExitCode;status='passed';host_evidence=@($netH -split "`n" | Where-Object {$_ -match 'NET lobby rejected|net-test: game begin|net-test: rejected|net-test: done'});client_evidence=@($netC -split "`n" | Where-Object {$_ -match 'NET lobby rejected|net-test: game begin|net-test: rejected|net-test: done'})}
        [IO.File]::WriteAllText((Join-Path $netOutput 'results.json'),(@{status='in progress';cases=$netRecords}|ConvertTo-Json -Depth 8),$netEncoding)
        Write-Output "NET_RULES PASS $netName rejection=$($netCase.reason)"
    } finally {foreach($netProcess in @($netServer,$netClient)){if($null -ne $netProcess -and !$netProcess.HasExited){Stop-Process -Id $netProcess.Id -Force -ErrorAction SilentlyContinue}}}
}
$netSha=[Security.Cryptography.SHA256]::Create();$netStream=[IO.File]::OpenRead($Exe)
try {$netExeHash=([BitConverter]::ToString($netSha.ComputeHash($netStream))).Replace('-','').ToLowerInvariant()}
finally {$netStream.Dispose();$netSha.Dispose()}
[IO.File]::WriteAllText((Join-Path $netOutput 'results.json'),(@{status='passed';exe_sha256=$netExeHash;cases=$netRecords}|ConvertTo-Json -Depth 8),$netEncoding)
Write-Output "NET_RULES complete=$($netRecords.Count)"
