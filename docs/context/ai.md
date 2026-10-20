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
  spark; Hard drives the full line. `CpuSkill::edge_room` sets how close
  to the road's edge the line may run (Hard 1 m, Easy 2.4 m), `weave` how
  far it sways across straights, `overtakes` whether it commits to the
  roomier side to pass a slower kart, and `mistakes` the chance in 100 of
  a slip in each 20 s spell (Normal about one a lap, Hard half that):
  a missed apex or an early drift release, or for Easy a brush against
  the wall (`cpu_mistake`, seeded from the tick and the kart index, so it
  stays a pure function of world state). `cpu_rubber_band(level)` sets
  `KartParams::rubber_band` (speed up behind the leading player, ease off
  ahead). `cpu_build(parts, seat)` picks each CPU's parts.
- Side roads: `cpu_route` picks which open ones a CPU takes each lap. Short
  of a fork it keeps its path on the main road up to the mouth (cutting
  the verge hits the fence beside it), and it keeps aiming down the side
  road for 10 m past the fork until the kart is properly on it. On a shut
  side road it heads back to the main road only while the barrier is still
  ahead; past it, or on a vanishing road, it drives on.
- `src/sim/racing_line.cpp`: the line itself, built per track in
  `Track::finalize()` by relaxing points towards least bending, clamped to
  the drivable road and steering round obstacles.
- `autopilot()` in `world.h` is the simpler line follower tests use.
- Check a change: `tests/test_ai.cpp`, and
  `gk_trackmap data/tracks/tracks.json out/ <track> --race` (12-CPU race:
  lap times, wall hits, falls).
