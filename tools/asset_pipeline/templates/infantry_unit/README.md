# Infantry unit template
#
# 1. Author in Blender (or drop PNGs into fixtures/stand/ as facing_FF.png)
# 2. Publish + QA:
#      python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/infantry_unit/manifest.yaml
#      python tools/asset_pipeline/qa_check.py --manifest tools/asset_pipeline/templates/infantry_unit/manifest.yaml
#
# Blender (optional):
#   blender --background --python tools/asset_pipeline/blender/ra2_iso_render.py -- \
#       --out tools/asset_pipeline/templates/infantry_unit/fixtures --demo --anim stand --frames 1
