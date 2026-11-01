# OpenRA2 asset production pipeline (SHP / VXL / vehicle_png)

Full usage: **[docs/asset-pipeline.md](../../docs/asset-pipeline.md)**

Quick fusion refresh:

```text
python tools/ra2pack/make_fusion_units.py
```

Dedicated cameo masters use `icon_source` relative to the manifest. The Type99 sample
is versioned under `art/cameos/`; `publish_unit.py --icon-only --manifest ...` rebuilds
only its opaque 108×84 menu icon, and QA rejects a fallback overwriting that master.
See the main asset-pipeline document for source constraints and provenance.
