# Type99 — fusion tank (vehicle_png)
#
# Engine uses PNG body+turret (vxl.cpp stems null). This remains a stylized Rhino derivative,
# not an original Type99 model. A dedicated VXL can replace it later via kind: vehicle.

From the repository root, export all eight native Rhino body/turret layers first:

```powershell
$fusionExport = Start-Process -FilePath build-verify/Release/ra2.exe -ArgumentList '--dump-vxl','--all-facings' -Wait -PassThru -WindowStyle Hidden
if ($fusionExport.ExitCode -ne 0) { throw 'Native VXL export failed' }
python tools/asset_pipeline/prepare_fusion_units.py --type99-only --native-dir tools/ra2pack/out
python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/type99/manifest.yaml
python tools/asset_pipeline/qa_check.py --manifest tools/asset_pipeline/templates/type99/manifest.yaml
```

The manifest declares the shared ground anchor at 0.72 of canvas height. Body and turret
PNGs have matching `_remap.png` sidecars that carry the palette house band and shade.
Keep those sidecars with the PNGs when copying or overriding this asset. The game only
uses a mask from the same content directory as its PNG, so a mod replacing the PNG alone
does not accidentally inherit a base mask. Outputs/fixtures are local generated assets.

## Independent cameo

The manifest points at the versioned `art/cameos/type99-v1.png` master. Full publication
preserves that source-based icon rather than constructing a blue-background sprite
thumbnail. To refresh only the icon without touching body/turret PNGs, run:

```powershell
python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/type99/manifest.yaml --icon-only
```

The master, final 108×84 image, and in-game camera are different artifacts. A new
cameo is not evidence that the underlying Rhino-derived battlefield model changed.
