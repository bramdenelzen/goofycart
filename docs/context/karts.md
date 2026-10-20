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
  take-off at speed (a lip, crest or ledge) open a trick window; beyond it a
  trick works anywhere in the air within `trick_max_height` of the road
  (`can_air_trick`: not gliding, underwater or on a drift hop). Tricks chain
  `trick_chain_frames` apart (`trick_gap`); one boost pays out on landing,
  only after `trick_min_air_frames` of air.
- Crests: `crest_throws` lifts the kart off when speed^2 x the centre line's
  bend beats `crest_stick` x g, keeping the climb from `crest_carry` m back.
  Landing needs the kart to be moving into the road.
- Drift hop: past the top of the hop with the stick held over, the
  mini-turbo starts charging in the air (`step_air`); landing the other way
  starts it over.
- Loops (`Track::loop_radius` > 0): in the air the pull towards the road is
  at least v²/r, so a hop or a bump comes back to it; on the ground the
  slope term of gravity is skipped (the road's down is down) and the grip
  descent gain is capped at 15% (`kLoopGainCap`, provisional).
- Snaking: with the stick held away from the drift (`drift_turn_wide`,
  0.08) the kart runs nearly straight while charging at `charge_slow`, so
  alternating drifts down a straight earn a blue mini-turbo each (about
  every 2.5 s) and gain a few percent on flat out while swinging about
  3 m either side of the line; `tests/test_kart.cpp` "snaking" keeps it
  between 1% and 8%.
- Two wheels (`KartState::tilt`, + right side down, in `step_suspension`):
  sideways force past `tilt_lateral_light`..`heavy` (30..38 m/s², by
  weight) tips the whole kart onto its outside wheels, `tilt_per_lateral`
  per m/s² up to `tilt_max`, on a softer spring that overshoots a flick and
  bounces when the wheels slam down. Normal steering at 150cc stays flat;
  a tight drift lifts ~0.3 rad, 200cc hits the cap. Visual only and synced
  on the wire; the client pivots on the outside wheels (bikes lean in).
- Grip road (`CenterSample::grip`, `Track::grip_up`): airborne gravity,
  yaw and the landing test use the road's up instead of world up, so hops
  off an 80° bank come back to it; `align_up` keeps the heading when the
  road's up changes. A descent raises `top` by `grip_descent_gain` per
  unit of downward slope; `grip_carry` frames of `grip_carry_decel` ease
  the extra speed off afterwards; drifts charge `grip_charge_mult` faster.
  Conveyors and currents move the kart through `current_at` after the
  step. Tests in `tests/test_mechanics.cpp`.
- Rocket start timing is in `kart_on_go`.

## Data

- `data/physics.json`: engine class multipliers, Level curves, constants.
  Values may be bare or `{"value", "source"}`; keep `source` honest.
- `data/parts.json`: 16 drivers, 10 bodies, 8 tyres, 5 gliders. Each part
  has `points` and a `look` the client builds the mesh from (no art files).
- `docs/BALANCE.md`: top-speed vs mini-turbo balance, measured with
  `gk_trackmap data/tracks/tracks.json out/ --balance`.

Changing physics or parts changes the race: bump `kProtocolVersion`.
