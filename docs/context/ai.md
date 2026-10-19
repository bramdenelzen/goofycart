# CPU drivers

- `src/sim/ai.cpp` / `ai.h`: `cpu_input(world, kart, skill)` returns a
  `KartInput` each tick. It follows the racing line with pure pursuit,
  hops into drifts on corners that need one, tricks off ramps, times
  rocket starts, heads for item boxes, steers round karts, bananas and
  obstacles, and uses items with intent (shells at the kart ahead, shields
  held behind, mushrooms on straights, horn against incoming shells).
- It is a pure function of world state: no memory between ticks and no
  randomness outside the world. Online clients predict CPU karts exactly
  as the server drives them, so keep it that way.
- Difficulty: `CpuLevel` Easy/Normal/Hard -> `cpu_skill(level, seat)`.
  Easy keeps to the middle and never drifts; Normal releases at first
  spark; Hard drives the full line. `cpu_rubber_band(level)` sets
  `KartParams::rubber_band` (speed up behind the leading player, ease off
  ahead). `cpu_build(parts, seat)` picks each CPU's parts.
- `src/sim/racing_line.cpp`: the line itself, built per track in
  `Track::finalize()` by relaxing points towards least bending, clamped to
  the drivable road and steering round obstacles.
- `autopilot()` in `world.h` is the simpler line follower tests use.
- Check a change: `tests/test_ai.cpp`, and
  `gk_trackmap data/tracks/tracks.json out/ <track> --race` (12-CPU race:
  lap times, wall hits, falls).
