# OpenRA2 Architecture

This document is the module map for contributors and AI agents. Fidelity contracts live in `docs/ra2-fidelity.md` and `docs/ra2-ui-fidelity.md`; they do **not** override these boundaries.

## Layers (dependency direction)

```
core/          RNG, math, INI helpers
    ↓
game/          World simulation, data tables, map, AI, campaign, scripts
    ↓
gfx/ + sfx/    Presentation assets (sprites, VXL, audio) — read by Game
    ↓
Game (app)     Input, UI, render orchestration, net session, CLI tests
```

- Lower layers must not include higher layers.
- `World` must not call raylib UI, play SFX, or touch network sockets.
- `Game` owns presentation and player intent; it issues `World::Cmd` and reads `World` for drawing.

## Hard boundaries

| Concern | Owns | Must not |
|---------|------|----------|
| **World** | Deterministic sim: entities, combat, harvest, production, SW, fog, pathing, save blobs, `Cmd` / `applyCmd` / `checksum` | UI, cursors, camera, raylib draw calls, Winsock |
| **Game** | App phases, input, camera, HUD/menus, rendering, LAN lockstep glue, smoke/CLI harnesses | Embed new combat/economy rules; put sim side effects outside `applyCmd`→`update` |
| **gfx / sfx** | Asset load/gen, draw helpers | Mutate `World` sim state |
| **net/** | TCP frame transport | Game rules or checksum logic (those stay on `World` / `Game` session) |

## Lockstep invariants

1. All player intent enters as `World::Cmd`.
2. Per tick: apply local cmds → apply remote cmds → `World::update()`.
3. `World::checksum()` must cover **all** sim-affecting state (ents, map ore/fog, players, projectiles, crates, RNG, etc.).
4. UI / SFX / camera are **not** in the checksum; they must not diverge sim.

## File ownership

### Simulation (`src/game/`)

| File | Role |
|------|------|
| `world.h` | `World`, `Ent`, `Cmd` declarations (facade) |
| `world.cpp` | Init, spawn/kill, thin orchestration |
| `world_queries.cpp` | Spatial / prereq / visibility queries |
| `world_orders.cpp` | `order*` / chrono jump |
| `world_production.cpp` | Queues, place/sell/repair, factory spawn |
| `world_pathing.cpp` | Movement along path |
| `world_combat.cpp` | Fire, damage, explode, projectiles |
| `world_harvest.cpp` | Harvester economy loop |
| `world_buildings.cpp` | Building tick, garrison fire |
| `world_superweapons.cpp` | SW launch / update |
| `world_fog.cpp` | Fog / gap shroud |
| `world_special.cpp` | Mind control, spy, crates, bombs |
| `world_save.cpp` | Save/load + ser helpers; prefer keep `applyCmd`/`checksum` nearby |
| `world_update.cpp` | `update` / `updateUnit` / `updateAircraft` when extracted |
| `data.*` / `map.*` / `ai.*` / `campaign.*` / `script.*` | Tables, map, AI, missions, Lua |

### Application (`src/game/game*`)

| File | Role |
|------|------|
| `game.h` / `game.cpp` | `Game` type, init, `run`, remaining glue |
| `game_hud.cpp` | Sidebar, bottom bar, minimap chrome |
| `game_menu.cpp` | Main / setup / mission select |
| `game_net.cpp` | Issue cmds, lobby, lockstep advance |
| `game_settings.cpp` / `settings.cpp` | Settings UI vs persistence |
| `game_editor.cpp` | Map editor |
| `game_tests.cpp` | Smoke / campaign matrix / play-test / bench |
| `game_input_sim.cpp` | Sim-mode input wrappers |
| `game_coords.cpp` | Screen ↔ world math |
| `game_camera.cpp` | Camera scroll/zoom |
| `game_save.cpp` | File save/load orchestration |
| `game_cursor.cpp` | Cursor assets and hover |
| `game_triggers.cpp` / `game_campaign.cpp` | Campaign triggers / waves |
| `game_render_world.cpp` | Terrain bake, entity/effect/fog draw |
| `game_input.cpp` | Selection and order issuing |
| `game_logic.cpp` | Per-tick app logic (AI tick, EVA, win/lose glue) |

## Checklist: changing sim state

When adding or changing a field that affects gameplay:

1. Update `World::Ent` / `Player` / related structs in `world.h`.
2. Update `World::checksum()` (same semantic coverage).
3. Update `saveGame` / `loadGame` (and any `ser*` / `deser*` helpers).
4. Put logic in the matching `world_*.cpp` — **do not** grow `world.cpp` / `game.cpp` with new systems.
5. Prefer extending `Cmd` + `applyCmd` for new player intents.

## Graphics policy

Three presentation paths exist (procedural sprites, PNG overlays, VXL). Prefer extending the **asset overlay** path for new art; do not add a fourth parallel look system without an architecture update.

## Status (rescue complete)

- `game.cpp` / `world.cpp` are thin facades; logic lives in `game_*.cpp` / `world_*.cpp`.
- Cursor rules enforce layering and subsystem placement (see `.cursor/rules/`).
- Refactor freeze has been lifted: new features are allowed if they land in the correct subsystem file.

## Tests

- Headless logic: `ctest` / `tests/logic_tests.cpp`
- Integration: `ra2.exe --smoke`, `--smoke-campaign`, `--campaign-matrix`, `--play-test`
- Net: dual-process host/client drivers

Refactor PRs must keep these green; mechanical moves must not change checksum semantics.
