# Visual audit (trust loop)

Two different reviews — do not mix them:

1. **Building sprite assets** (PNG files under `assets/sprites`) — if these are wrong, cage/mk fixes cannot help.
2. **In-game render shots** (`--visual-audit`) — only after assets look correct.

## 1) Building asset review (start here)

```bat
python tools/visual_audit/export_bld_review.py
```

Writes:

| Path | What |
|------|------|
| `tools/visual_audit/bld_review/singles/NN_name.png` | One labeled card per `bld_*.png` |
| `tools/visual_audit/bld_review/sheet_XX.png` | Contact sheets |
| `tools/visual_audit/bld_review/index.txt` | Checklist (MIX id, size, auto flags) |

Open `bld_review/` in Explorer and mark each row in `index.txt` as OK / WRONG.

## 2) In-game capture (after assets OK)

```bat
cmake --build build --config Release --target ra2
build\Release\ra2.exe --visual-audit
python tools/visual_audit/analyze.py
python tools/visual_audit/compare_cage.py
```

## Selection cage (RA2-style Foundation + Height)

No per-building pixel overrides.

- **Bottom face:** dashed iso diamond from `BldDef` footprint `w×h`
  - `halfW = (w+h)*TILE_W/4`, `halfD = halfW/2`
  - south tip = `bldScreenPos`
- **Height:** `elev = art.ini Height * TILE_H` (generated into `bld_cage_data.inc`)

```bat
python tools/visual_audit/gen_bld_cage_from_art.py
python tools/visual_audit/export_bld_cage_review.py
```

Review: `tools/visual_audit/bld_review/cage/singles/`
