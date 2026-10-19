# Karts, physics and stats

## Files

- `src/sim/kart.cpp` / `include/gk/sim/kart.h`: the kart controller.
  `kart_step` is the entry; it dispatches to `step_ground`, `step_air`
  (jumps, tricks, gliding), `step_respawn`, `collide_walls`,
  `step_suspension`, `update_water` (deep water, underwater mode),
  `update_roof` (tunnels). `KartState` holds everything per kart;
  `KartStepContext` passes track, constants, params and the event sink.
- `src/sim/stats.cpp` / `stats.h`: the MK8 Deluxe stat system. Each part
  in `data/parts.json` adds points per stat (11 stats: speed ground/water/
  air, acceleration, weight, handling x3, traction, mini-turbo,
  invincibility); the sum is a Level 0..20 looked up in
  `data/physics.json` curves to give `KartParams`. `PhysicsConstants` are
  the shared rules (gravity, hop, drift, boost frames...).
- `src/sim/include/gk/sim/surface.h`: surface classes (road, offroad
  light/medium/heavy, sand, ice, water, ...).

## Model

Arcade, not rigid-body: speed is set by rules along the heading, steering
is not a torque, physics only adds gravity, landings and knock-back
(header comment of `kart.cpp`). Jolt is used only for ray and sphere
queries against the track.

- Drift: hop, then `Drifting`; mini-turbo tiers blue/orange/purple
  (`mt_tier`), released as `BoostKind::MiniTurbo` etc. Outward vs inward
  drift by body (`DriftType`).
- Boosts: `kart_give_boost(kind, frames, mult, ignores_offroad)`; kinds in
  `BoostKind` (mini-turbos, trick, pad, glide ramp, rocket start, mushroom,
  star, bullet, slipstream).
- Glider: only a glide ramp opens it. Height and speed are one energy
  budget (diving gains speed, climbing spends it); stick rolls into a bank.
  Background: `docs/research/glider-flight-models.md`, tests in
  `tests/test_glider.cpp`.
- Tricks: launch surfaces (trick/glide ramps, pads, vents) and any natural
  take-off at speed (a lip, crest or ledge) open a trick window; the boost
  pays out on landing only after `trick_min_air_frames` of air.
- Rocket start timing is in `kart_on_go`.

## Data

- `data/physics.json`: engine class multipliers, Level curves, constants.
  Values may be bare or `{"value", "source"}`; keep `source` honest.
- `data/parts.json`: 16 drivers, 10 bodies, 8 tyres, 5 gliders. Each part
  has `points` and a `look` the client builds the mesh from (no art files).
- `docs/BALANCE.md`: top-speed vs mini-turbo balance, measured with
  `gk_trackmap data/tracks/tracks.json out/ --balance`.

Changing physics or parts changes the race: bump `kProtocolVersion`.
