# Fusion cameo source art

`type99-v1.png` is an original Type99-style tank illustration generated with the built-in imagegen tool on 2026-10-31 for this project. It is not a Westwood MIX extraction. The icon does not change or replace the battlefield unit model, which remains the documented stylized Rhino-derived PNG body/turret.

- Master: 1422×1106, opaque PNG, 9:7, no text or UI frame.
- SHA-256: `a02ce18e7f7cd1dfa7c28b5842af0a35e7d6bf6c29c5a2d1844b844758c7103e`.
- Art direction: low three-quarter front-left olive tank, angular armor, upper-left cannon and key light, red recognition panel, muted blue-grey sky, dusty ground, readable silhouette at sidebar size.
- Build source: `tools/asset_pipeline/templates/type99/manifest.yaml` → `icon_source`.
- Output: `assets/sprites/icon_unit_type99.png`, opaque 108×84 with at most 256 colors; localized name and state indicators are drawn by the game.

Keep this versioned master in source control. Builds normalize this saved image rather than calling image generation; AI re-generation would not reproduce the same pixels. Use a new versioned source filename for a deliberate art revision. Generated runtime images remain ignored, while `.gitignore` explicitly includes this source directory.
