# OpenRA2 development instructions

## Scope and change discipline

- Work on the user's current objective. A review or proposal alone does not authorize implementation. Later explicit authorization takes precedence.
- Define one observable problem, affected files and acceptance criteria before a change. Finish that scope before expanding features, campaign coverage or source-fidelity research.
- Reuse the current checkout. Preserve unrelated dirty files, user saves, mods and existing evidence. Do not stage, commit, reset or clean unless requested.
- Keep mechanical extraction separate from simulation behavior changes. Do not add a compatibility switch for a pure refactor.
- Use the current architecture in `docs/architecture.md` and the bounded workflow in `docs/development.md`. Dated batch documents describe historical results.

## Runtime boundaries

- `ra2_runtime` contains shared product code. `ra2` is the shipping executable. Native regression entrypoints belong to `GameRegression` and `ra2_regression`; do not add test methods or `RA2_TESTING` branches to runtime objects.
- `World` owns simulation state. UI/rendering consume state and presentation events; rendering must not mutate simulation state or RNG.
- Use `World::applyCmd` and existing order admission for player/AI commands. Retain generation-bound identities in delayed references.
- Change an existing actor's owner through `World::changeActorOwner`. Creation, decoding, temporary projections and projectile/event attribution are distinct operations. Finish building ownership batches at their established `synchronizeBuildingOwners` boundary; preserve controller cleanup and factory-job scheduling.
- Reuse `actor_state_transfer.h`, `actor_contents.h`, `binary_state_io.h` and `production_work.h` where their contracts apply. Live traversal excludes dead slots; persistence traversal includes historical slots and has its own bounds.
- Keep ordinary/skirmish and authored-mission AI scheduling and price policies explicit. Share execution primitives rather than inventing a new universal AI framework.
- Historical scene/save contracts are centralized in the format/contract headers. Keep supported decoding and checksums intact. Unsupported combinations must reject atomically, with the active game unchanged.
- Pure structural edits keep formats/protocol unchanged. Simulation semantics require network compatibility review. Increment an unpublished protocol cohort once when necessary, not on every edit; save versions change only when their encoded contract changes.

## Build and verification

Run from the repository root, with CMake/CTest and Python available:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
python -X utf8 -B tests/verify.py --build build --tests ra2.ai_attack ra2.attack_move
python -X utf8 -B tests/verify.py --build build --all
```

- `tests/verify.py` builds by default, verifies actual executed tests and records executable/input hashes. Use `--no-build` only deliberately after a completed build. `--list-tests` is inventory, not verification.
- Select related tests for each leaf change. Run the complete Release suite once the declared batch is stable. Repeat only after relevant changes, failures or unresolved concerns.
- Run native simulation/performance tests serially, including across Release and ASan processes. Initialize `Game` once per native process; use separate processes for separate entrypoints.
- For memory/lifecycle/persistence changes, configure a separate `build-asan` with `-DRA2_ASAN=ON` and run the related checks. Supply the matching MSVC ASan runtime directory with verifier `--runtime-dir` if it is absent from PATH.
- Keep test windows, fixed seeds and outcome assertions unchanged when fixing a failing match. No free units, forced victories or seed-specific behavior in production code.
- A skipped, unexecuted or timed-out check is not a pass. Distinguish mechanisms, natural matches, replay, persistence, screenshots and human playtesting.
- Save comparisons must use files produced by that run. Preserve failed logs and write each verification to a new output directory; do not rewrite sealed historical batches.
- Use `python -X utf8 -B` for tooling. Avoid running old batch preparers or sealers for routine work.

## Completion

Report the resulting behavior, changed boundaries, actual checks and remaining limits. Update current documentation when a contract or command changes. Do not promote experimental source AI, claim complete RA2 fidelity or begin another feature batch as an automatic continuation.
