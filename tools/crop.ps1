# crop.ps1 -src <in.png> -cx <x> -cy <y> -cw <w> -ch <h> -out <out.png> [-scale N]
param([string]$src, [int]$cx, [int]$cy, [int]$cw, [int]$ch, [string]$out, [int]$scale = 3)
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Bitmap]::FromFile($src)
if ($cx + $cw -gt $img.Width) { $cw = $img.Width - $cx }
if ($cy + $ch -gt $img.Height) { $ch = $img.Height - $cy }
$rect = New-Object System.Drawing.Rectangle($cx, $cy, $cw, $ch)
$crop = $img.Clone($rect, $img.PixelFormat)
$nw = $cw * $scale; $nh = $ch * $scale
$big = New-Object System.Drawing.Bitmap($nw, $nh)
$g = [System.Drawing.Graphics]::FromImage($big)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($crop, (New-Object System.Drawing.Rectangle(0, 0, $nw, $nh)))
$big.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $big.Dispose(); $crop.Dispose(); $img.Dispose()
Write-Output "saved $dst ($cw x $ch x$scale)"
