# Superweapon building states (verified against art.ini / rules.ini)

## Building appearance

| Building | Idle (not ready) | Ready (charged) | Notes |
|----------|------------------|-----------------|-------|
| Nuke silo | `bld_nukesilo.png` = closed (`NAMISL_E`) | `bld_nukesilo_ready.png` = SuperAnim E–H (doors open + missile) | Base `namisl` in this pack is pad-only; closed body is E |
| Weather | `bld_weatherdevice.png` = `GAWETH` | `bld_weatherdevice_ready.png` = SuperAnim E–H | rules `GAWEAT` Image=`GAWETH`; ready FX subtle in this MIX |
| Iron Curtain | `bld_ironcurtain.png` | `bld_ironcurtain_ready.png` = SuperAnim A/F/G/H | Ready glow subtle vs idle |
| Chrono Sphere | `bld_chronosphere.png` = static `ggcsph` | `bld_chronosphere_ready.png` = SuperAnim E–G (H skipped: bad oversized SHP) | Idle static looks incomplete vs ready in this MIX |

## Use / fire effects (not building SHP)

OpenRA2 draws launch FX procedurally in `src/game/game_hud.cpp` (mushroom cloud, lightning bolts, iron arcs, chrono swirl, genetic helix, force shield), **not** from MIX building SuperAnim.

| Layer | Role |
|-------|------|
| Building `Image` | Always visible structure |
| `SuperAnim*` | **Ready loop** while SW charged |
| Buildup mk | Build / sell |
| `SpecialAnim` / turret VXL | Attack / rotate (defenses) |
| HUD procedural | Launch / impact FX |

## Defenses (always have head when idle)

| Building | Idle head | Attack |
|----------|-----------|--------|
| Prism | base + `ActiveAnim=GAPRIS_B` | `SpecialAnim=GAPRIS_A` |
| Grand Cannon | base `gagcan` + `gtgcantur`/`gtgcanbarl` VXL | rotate / recoil |
| Sentry | base + `LASER` VXL | turret + PBarrel |
