param(
    [string]$src,
    [string]$dst,
    [int]$x = 0,
    [int]$y = 0,
    [int]$cw = 0,
    [int]$ch = 0,
    [int]$zoom = 2
)
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Bitmap]::FromFile($src)
if ($cw -le 0) { $cw = $img.Width - $x }
if ($ch -le 0) { $ch = $img.Height - $y }
$rect = New-Object System.Drawing.Rectangle($x, $y, $cw, $ch)
$crop = $img.Clone($rect, $img.PixelFormat)
$ow = $cw * $zoom
$oh = $ch * $zoom
$out = New-Object System.Drawing.Bitmap($ow, $oh)
$g = [System.Drawing.Graphics]::FromImage($out)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$dstRect = New-Object System.Drawing.Rectangle(0, 0, $ow, $oh)
$g.DrawImage($crop, $dstRect, 0, 0, $cw, $ch, [System.Drawing.GraphicsUnit]::Pixel)
$out.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $out.Dispose(); $crop.Dispose(); $img.Dispose()
Write-Host "saved $dst ($ow x $oh)"
