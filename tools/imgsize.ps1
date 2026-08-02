Add-Type -AssemblyName System.Drawing
foreach ($f in $args) {
    $i = [System.Drawing.Image]::FromFile($f)
    Write-Host "$f : $($i.Width) x $($i.Height)"
    $i.Dispose()
}
