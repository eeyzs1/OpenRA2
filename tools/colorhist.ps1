param(
    [string]$src,
    [int]$top = 25
)
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Bitmap]::FromFile($src)
$colors = @{}
for ($y = 0; $y -lt $img.Height; $y++) {
    for ($x = 0; $x -lt $img.Width; $x++) {
        $p = $img.GetPixel($x, $y)
        if ($p.A -gt 200) {
            $k = "$($p.R),$($p.G),$($p.B)"
            if ($colors.ContainsKey($k)) { $colors[$k]++ } else { $colors[$k] = 1 }
        }
    }
}
$img.Dispose()
$colors.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $top | ForEach-Object { "$($_.Name) : $($_.Value)" }
