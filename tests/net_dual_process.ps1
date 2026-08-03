param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$Frames = 360,
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$tag = "{0}-{1}" -f $PID, [DateTime]::UtcNow.Ticks
$hostOut = Join-Path $env:TEMP "openra2-net-host-$tag.log"
$hostErr = Join-Path $env:TEMP "openra2-net-host-$tag.err"
$clientOut = Join-Path $env:TEMP "openra2-net-client-$tag.log"
$clientErr = Join-Path $env:TEMP "openra2-net-client-$tag.err"
$hostProc = $null
$clientProc = $null

try {
    $hostProc = Start-Process -FilePath $Exe -ArgumentList @("--net-host", "$Frames") `
        -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru
    $null = $hostProc.Handle
    Start-Sleep -Milliseconds 500
    $clientProc = Start-Process -FilePath $Exe -ArgumentList @("--net-client", "$Frames") `
        -RedirectStandardOutput $clientOut -RedirectStandardError $clientErr -PassThru
    $null = $clientProc.Handle

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ((!$hostProc.HasExited -or !$clientProc.HasExited) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $hostProc.Refresh()
        $clientProc.Refresh()
    }
    if (!$hostProc.HasExited -or !$clientProc.HasExited) {
        throw "network self-test timed out after $TimeoutSeconds seconds"
    }
    $hostProc.WaitForExit()
    $clientProc.WaitForExit()
    $hostProc.Refresh()
    $clientProc.Refresh()

    $hostLog = ((Get-Content $hostOut -Raw -ErrorAction SilentlyContinue) + "`n" +
                (Get-Content $hostErr -Raw -ErrorAction SilentlyContinue))
    $clientLog = ((Get-Content $clientOut -Raw -ErrorAction SilentlyContinue) + "`n" +
                  (Get-Content $clientErr -Raw -ErrorAction SilentlyContinue))
    Write-Host "HOST LOG`n$hostLog"
    Write-Host "CLIENT LOG`n$clientLog"

    if ($hostProc.ExitCode -ne 0 -or $clientProc.ExitCode -ne 0) {
        throw "network processes failed: host=$($hostProc.ExitCode) client=$($clientProc.ExitCode)"
    }
    $pattern = "net-test: done tick=(\d+) checksum=([0-9a-fA-F]+) desync=0"
    $hostMatch = [regex]::Match($hostLog, $pattern)
    $clientMatch = [regex]::Match($clientLog, $pattern)
    if (!$hostMatch.Success -or !$clientMatch.Success) {
        throw "missing successful final checksum record"
    }
    if ([int]$hostMatch.Groups[1].Value -lt $Frames -or [int]$clientMatch.Groups[1].Value -lt $Frames) {
        throw "network processes stopped before requested tick"
    }
    if ($hostMatch.Groups[2].Value -ne $clientMatch.Groups[2].Value) {
        throw "final checksum mismatch: host=$($hostMatch.Groups[2].Value) client=$($clientMatch.Groups[2].Value)"
    }
    Write-Host "NET DUAL PROCESS PASS tick=$Frames checksum=$($hostMatch.Groups[2].Value)"
} catch {
    Write-Error $_
    exit 1
} finally {
    foreach ($proc in @($hostProc, $clientProc)) {
        if ($null -ne $proc -and !$proc.HasExited) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item $hostOut, $hostErr, $clientOut, $clientErr -Force -ErrorAction SilentlyContinue
}
