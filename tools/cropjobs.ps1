# cropjobs.ps1 — 自包含批量裁剪（避免外部参数被包装器干扰）
Add-Type -AssemblyName System.Drawing
function Crop($src, [int]$cx, [int]$cy, [int]$cw, [int]$ch, $dst, [int]$scale) {
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
}
Crop "tools\ref_yr_ingame.png" 890 0 131 300 "tools\z_ref_sb_full.png" 4
Crop "tools\ref_yr_ingame.png" 0 540 1021 34 "tools\z_ref_bottombar.png" 3
Crop "tools\ref_yr_ingame.png" 890 300 131 274 "tools\z_ref_sb_grid.png" 4
Crop "pt_04_placed.png" 300 150 250 180 "tools\z_our_bld.png" 3
Crop "pt_04_placed.png" 840 0 184 576 "tools\z_our_sb.png" 1
