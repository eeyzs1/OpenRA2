# PLA — fusion infantry template
#
# 1. Extract official sprites (once): python tools/ra2pack/gen_assets.py
# 2. Build fixtures: python tools/asset_pipeline/prepare_fusion_units.py --pla-only
# 3. Publish: python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/pla/manifest.yaml
# 4. QA: python tools/asset_pipeline/qa_check.py --manifest tools/asset_pipeline/templates/pla/manifest.yaml

The walk/fire rates in the manifest are presentation frame holds, independent of the
source sequence's facing stride. Re-extract Conscript first after sequence parser changes;
the PLA fixtures inherit its corrected walk/fire/death/deployed frames.
