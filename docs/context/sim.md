# Simulation core (`gk_sim`)

`src/sim` is the pure race simulation shared by client, server, bots, CPU
drivers, ghosts and tools. It links only `gk_core`, glm, nlohmann_json and
Jolt (queries only). No rendering, window, audio, clock or OS calls.

## Shape

- `world.h` / `world.cpp`: `World` owns the `Track`, `PhysicsTables`,
  `RaceConfig` (engine class, laps, countdown, items on/off, seed), karts,
  item entities, box respawns, breakable props, track switches and the RNG.
  `World::tick(span<const KartInput>)` steps everything once at 60 Hz
  (`gk::kTickRate`, `src/core/include/gk/core/fixed_step.h`).
- Tick order (`World::tick`): items before step, `kart_step` per kart,
  slipstream, props, switches, kart-kart collision, items after step,
  gates, then lap/section progress. Item code is in
  `items.cpp` but is `World` member functions (see `items.md`).
- `save()` / `restore()` copy `WorldState`: everything needed to rewind for
  client reconciliation. New mutable state must go into `WorldState` or
  prediction breaks.
- `events()` are per-tick `KartEvent`s (boosts, hits, laps, item use) that
  the client turns into sound and particles; the sim never plays anything.
- `input.h` `KartInput` is the only way anything drives a kart: players,
  CPUs, the Bullet's autopilot and ghosts.
- `ghost.h`: a Time Trial ghost is only quantised inputs; `GhostRunner`
  replays them in its own `World`. `ghost_still_valid` replays once and
  drops ghosts the current physics can no longer reproduce.
- `data.h` `data_path()` finds `data/` ($GK_DATA, ./data, next to the exe,
  then the source tree).

## Determinism

Same inputs and seed give the same race. Randomness only from the world's
`Rng`; no `std::rand`, time or unordered iteration in sim logic. Net
snapshots are quantised, so bit-exactness across machines is not required:
the server corrects clients. The CPU driver reads only world state, which
is why clients can predict CPU karts.

## Tests

One doctest binary (`tests/`, ~110 cases, files by area: `test_kart`,
`test_race`, `test_items`, `test_glider`, `test_water`, `test_tunnels`,
`test_tracks`, `test_point_to_point`, `test_paths`, `test_props`, `test_ai`,
`test_ghost`, `test_net`, `test_master`, `test_audio`, `test_core`).
`tests/sim_fixture.h` gives `test::tables()`, `test::parts()`,
`test::default_params()` and `test::racing_world(n)` (greybox track,
countdown skipped). Tests load the real `data/*.json`.
