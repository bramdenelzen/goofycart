# Track files

Every track is a JSON file in `data/tracks/`, named by its id and listed in a
cup in `data/tracks/tracks.json`. `gk_trackmap data/tracks/tracks.json out/ [id] [--race]`
draws each one from above (main road green line, side roads cyan, barriers
orange, switches yellow, zones hatched, tunnels as a dark ribbed stripe) and
with `--race` runs a 12-CPU race on it.

## The main loop

```json
{
  "id": "meadow_circuit", "name": "Meadow Circuit", "theme": "meadow", "ground": true,
  "half_width": 7.5, "verge": 5, "verge_surface": "offroad_light",
  "points": [[0, 0, -120], [0, 0, -30, 9.5], ...],
  "features": [...], "gates": {...}, "branches": [...]
}
```

`theme` picks the client's palette, scenery, sky and music: `meadow`,
`desert`, `harbor`, `snow`, `canyon`, `volcano`, `sky`, `night`, `market` or
`beach` (sand, palms, the sea all round and a sea floor of coral and kelp
wherever the road dips under; its tunnels are wrecks).

`points` are the closed loop's control points: x, y (up), z and an optional
road half-width for that point. Widths blend smoothly between points, so a
road can pinch to a narrow bridge and open into a wide plaza.

Positions along the loop are `[control point, metres]`: `[4, 20]` is 20 m
past point 4.

## Point to point

A track built around one run instead of laps:

```json
{
  "id": "summit_descent", "name": "Summit Descent", "theme": "snow", "ground": true,
  "point_to_point": true, "start": [1, 0], "finish": [24, 0], "sections": [[11, 0], [19, 0]],
  "points": [[0, 170, 560, 9.5], ..., [65, 0, -720, 9.5]], ...
}
```

With `point_to_point` the spline is an open road from its first point to
its last, with a wall across each end. The race is one run from the
`start` line to the `finish` line (default: 60 m in and 80 m before the
end): the grid forms on the road before the start line, so leave 50 m of
road before it, and the road runs on past the finish for the karts to roll
out on. Progress is shown in sections instead of laps: `sections` lists
where each section after the first begins (two entries for three sections),
or is a count for equal parts (default 3). The rest of the file is as a
loop's, with `[point, metres]` positions along the road; a gate's `laps`,
`lap` and `from_lap` read the leader's section, and `sections`, `section`
and `from_section` say the same thing in the track's own words.

Every mode races it the same way: VS and Grand Prix place by distance along
the road, Time Trial ghosts replay it, online rooms run it, the CPUs drive
it, and the HUD counts "SECTION 2/3" where a loop counts laps. The road is
one way, so there is no lap length to go round twice: make it long. The
checkpoints run from the start line to the finish, which begins the last
checkpoint region; entering it in order is finishing. A glide on a descent
lands only where the road falls away slower than the glider sinks (about 1
in 6), so flatten the road after a glide ramp or the glider carries on.
Summit Descent is the example.

## Features

On the main loop, `at`, `from` and `to` are `[point, metres]`; on a branch
they are metres from where the branch leaves.

| type | what | keys |
|---|---|---|
| `trick_ramp` | a hump that launches into a trick | `at`, `length`, `height` |
| `glide_ramp` | a blue ramp over a chasm | `at`, `length`, `height`, `gap` |
| `boost_pad` | dash panel | `at`, `length`, `lanes` (0..3) |
| `road` | a stretch of another surface | `from`, `to`, `surface` |
| `verge` | wider or different verges | `from`, `to`, `side`, `width`, `surface` |
| `no_walls` | open edges | `from`, `to`, `side` |
| `item_boxes` | a row of four boxes | `at` |
| `obstacle` | a block or pillar (`prop`: crate, barrel, crystal, tires, solid) | `at`, `offset`, `shape`, `width`, `length`, `height` |
| `mover` | a moving platform, block, press or swing | `at`, `kind`, `travel`, `period`, `phase`, `drop`, `arm`, `angle` |
| `stairs` | main loop only: the road drawn as steps | `from`, `to` |
| `barrier` | a wall that stands while its gate holds | `at`, `when`, `width` (0 = whole road), `offset`, `thickness`, `height` |
| `switch` | a pad; driving over it presses it | `at`, `name`, `offset`, `width`, `hold` (seconds) |
| `zone` | the road's surface changes while its gate holds | `from`, `to`, `surface`, `when`, `side` or `offset` + `width` |
| `tunnel` | a roofed stretch: an arched vault over the road | `from`, `to`, `height` (m, default 7), `style` |
| `bank` | the road tilted across, easing in and out over 15 m | `from`, `to`, `angle` (degrees, + the right side lower; at most 35, or 80 on grip road) |
| `grip` | grip road: karts stick to it however steep it banks, and gain speed downhill | `from`, `to` |
| `conveyor` | dry road that moves, carrying karts along or across | `from`, `to`, `push` (m/s along), `drift` (m/s across, + right), `when` |
| `wave` | the road rolls in travelling waves | `from`, `to`, `amplitude` (m), `wavelength` (m), `period` (s), `phase`, `when` |
| `current` | deep water only: the water moves, carrying karts with it | `from`, `to`, `push` (m/s along), `drift` (m/s across, + right), `when` |
| `vent` | deep water only: a bubble vent that throws karts up | `at`, `offset`, `width`, `lift` (m/s up), `period`, `open`, `phase`, `when` |
| `loop` | main road only: a vertical loop spliced in at `at` | `at`, `radius` (8..30 m, default 12), `shift` (m sideways, + right; at least 2 x half width + 4, the default) |

A loop is built into the road, not drawn with the control points: the road
climbs into a clothoid (a circle of `radius` with the curvature easing in
over the first `radius` metres and out over the last), comes back down
`shift` metres to the right of where it left, and eases back onto its own
line over the next 60 m or so. The road has to run straight and level from
12 m before the loop to about 80 m after it, nothing that lifts, tilts or
roofs the road (ramps, gaps, banks, waves, tunnels, stairs) can sit on it,
and it is always grip road with 10 m of grip either side. Karts stick to it
at any speed, gravity on it is the road's down (no speed lost climbing, and
at most 15% gained coming down), a hop or a hit on it stays on it, and
nobody respawns onto it. The lap gets longer by the loop's length less the
straight it consumed; features placed by later control points stay where
they were. More than one loop is fine: Caldera Coaster has two, one at the
foot of its descent and one straight off a glide landing.

Zone surfaces: `water`, `ice`, `oil`, `sand`, `offroad_*`, `boost` (a dash
strip) and `lava` (out of bounds: a respawn).

A `water` zone is shallow by default: the road's surface becomes water, with
its own speed. `"depth": "deep"` makes it a volume of water instead, driven
under: the road keeps its own surface and the karts switch to the underwater
mode (the water stats for top speed and steering, drifts a little wider,
half gravity and a capped fall in the air, the stick tipping the nose like
the glider's, the glider folded away). A kart goes under once its middle has
been below the water for 6 frames, and comes up the same way; no speed is
lost either way. The water's surface is level with the lower of the road's
two heights at the zone's ends, or `"level"` (metres) to set it. Dip the
road under it with the control points:

```json
{"type": "zone", "from": [18, 0], "to": [20, 0], "surface": "water", "depth": "deep"}
```

CPUs drive through deep water like road. Harbor Hop's dip under the west
quay is the example.

Deep water can move. A `current` carries every kart under it: `push` metres
per second along the road (negative against the traffic) and `drift` across
it (positive to the right), as a steady shove on top of the kart's own
driving. A `vent` is a bubble column `width` metres across at `offset` from
the centre line; a kart driving over an active one is thrown straight up at
`lift` m/s, with a trick window like a ramp's, and comes down wherever the
water puts it. With `period` (seconds) it blows for `open` seconds of each
period (half of it by default), starting `phase` (0..1) of the way through;
without one it blows all the time. Both take `when`, so a rip current can
come and go with a gate and a vent can wake on the last lap:

```json
{"type": "current", "from": [6, 20], "to": [8, 20], "push": 5},
{"type": "current", "from": [14, 10], "to": [15, 20], "drift": -2.5, "when": "rip"},
{"type": "vent", "at": [8, 30], "offset": 2.5, "width": 6, "lift": 13, "when": "last_lap"}
```

The CPU drivers know nothing of either; keep a current's drift gentle enough
to steer against and give a vent somewhere to land. Coral Cove's lagoon
(two fifths of the lap under the sea, a stream along the floor, a rip across
the grotto, a vent at each end and a wreck to cut through) is the example.

An `obstacle` with a `when` becomes a barrier, since the fixed road can't change.

A `bank` tilts the road (verges and walls with it) about its middle line.
A positive `angle` lowers the right-hand side, banking a right-hand bend;
a negative one on a right-hand bend is off-camber. Banks ease in and out over
their first and last 15 m (a third of them if shorter). Keep a bank off a
side road's junctions (the side road joins flat), and off stairs. On
terrain, the ground keeps below the road's low edge.

```json
{"type": "bank", "from": [7, 45], "to": [9, 20], "angle": -22}
```

### Grip road

A `grip` stretch is road the karts cannot fall off: a steep bank, a spiral
down a tower, a corkscrew. It is not anti-gravity. On the ground nothing
changes (the kart already follows the road's normal); in the air over it
gravity, steering and the kart's up all follow the road's up at the nearest
sample, so a hop or a bump off an 80° wall lands back on the wall instead of
dropping to the floor below. Banks steeper than 35° are allowed only where a
`grip` covers the whole bank (the parser refuses them otherwise), up to 80°;
their ease-in grows with the angle (15 m per 35°). Three things make it
worth racing:

* Going downhill on grip road raises the kart's top speed by half the slope
  (`grip_descent_gain`: a 1-in-2 descent is 25% faster), and the speed
  built there bleeds off gently for a second and a half after the grip ends
  instead of snapping back, so a spiral's exit is a slingshot.
* Drifts charge mini-turbos a quarter faster on it.
* The camera rolls with the road (up to 55°), so a wall ride reads as a wall
  ride; thrown items and dropped bananas follow the road's up too.

```json
{"type": "grip", "from": [11, 0], "to": [14, 10]},
{"type": "bank", "from": [11, 10], "to": [14, 0], "angle": -70}
```

Shape the spiral with the control points (a helix is just a loop of points
losing height; a banked tube works the same) and keep the `grip` a little
longer than the bank at both ends, so the ease-in and ease-out are covered.
Grip road wants no walls on its low side (`no_walls`) when the bank itself
holds the kart; a wall there reads as a ledge. CPUs drive it as ordinary
road. Vortex Quarry's corkscrew (a turn and a half of 60° road screwing
down into the pit) and the first turn of Cloud Spire's helix are the
examples.

### Conveyors

A `conveyor` is a `current` for dry road: a belt, a drifting ice floe, a
moving walkway. Every grounded kart on it is carried `push` m/s along the
road (negative against the traffic) and `drift` m/s across it (positive to
the right) on top of its own driving; airborne karts are not. It is drawn as
yellow chevrons on the road. With `when` a belt can run only while a gate
holds, so a switch can reverse it: two conveyors over the same stretch, one
with `push` and one with `-push`, gated on the switch and its opposite.

```json
{"type": "conveyor", "from": [4, 0], "to": [5, 20], "push": 6},
{"type": "conveyor", "from": [9, 0], "to": [10, 0], "push": -8, "when": "reverse"}
```

A conveyor against the traffic should be beatable at top speed (keep `push`
under about 10) and a crosswise one gentle enough to steer against; the CPUs
know nothing of either. Lava Works' conveyor shortcut and Scrapyard Shift's
intake and return belts (the shift lever reverses the return) are the
examples.

### Waves

A `wave` makes the road itself rise and fall: a boardwalk on a swell, a
rubber floor, a haunted corridor whose boards heave. The road's height is
raised by `amplitude` × sin over `wavelength` metres, the crests travelling
with the traffic one `wavelength` every `period` seconds, easing in and out
at the stretch's ends. The collision is analytic (the wave is a function of
the tick, so the server, the clients and the ghosts agree to the frame) and
the drawn road is displaced the same way in the vertex shader. Karts ride
the swell; a crest taken at speed is a take-off, so a wave is a trick
surface in its own right (tricks pay out only after 12 frames of air, so
rolling over a gentle swell gives nothing). `when` gates it.

```json
{"type": "wave", "from": [15, 0], "to": [17, 0], "amplitude": 0.8, "wavelength": 14, "period": 2.5}
```

Keep `amplitude` under about a metre (1.5 is the most the parser takes), the
`wavelength` at least four times the amplitude, and waves off bridges, banks
and junctions. Harbor Hop's pontoon start straight and Scrapyard Shift's
shaker floor are the examples.

### Presses and swings

A `mover` is a `platform` (karts ride it: `travel`, `period`, `phase`,
`ramp`, `boost`), a `block` (it shoves them), a `press` or a `swing`.

A `press` is a block that slams down onto the road: it rests `drop` metres
up for half its period, drops in a tenth of it, holds flat on the road for
a fifth and rises in the last fifth. A kart under it when it comes down is
squashed (a tumble, a `Squash` event: a crunch and a thump) and a kart on
the road beside it is untouched. The default press is 6 m across, 4 m along
the road and 2.4 m tall; `width`, `length` and `height` size it, `offset`
places it and `drop` (at least 1.5) sets how high it lifts. A kart can
drive under a raised press.

```json
{"type": "mover", "at": [6, 12], "kind": "press", "period": 3, "phase": 0.5, "drop": 3.5},
{"type": "mover", "at": [6, 24], "kind": "press", "period": 3, "phase": 0.0, "offset": -3, "width": 5}
```

A `swing` is a platform hanging from a pivot `arm` metres above its deck
that sways `angle` degrees either way about the road's direction, one full
swing per `period`: a gondola, a pendulum bridge. Karts on it are carried
with the deck, so a kart that sits still rides out to the end of each sweep
and a kart leaving at the far end has the deck's lean to deal with.
`height_above` lifts the deck above the road.

```json
{"type": "mover", "at": [10, 30], "kind": "swing", "arm": 8, "angle": 30, "period": 4, "width": 8, "length": 12}
```

Presses, swings, conveyors and waves are all functions of the race tick, so
they stay in step across the network and in ghosts; none of them needs a
gate to move. The CPU drivers know nothing of presses or waves: give a line
of presses a phase pattern a kart at top speed slips through, and keep
waves on straights. Lava Works' run home has two presses, Scrapyard Shift's
press hall three rows of them and its crane bay a swing.

A `tunnel` roofs the road from `from` to `to`, on the main loop or a branch:

```json
{"type": "tunnel", "from": [17, 13], "to": [19, -6], "height": 7.5, "style": "rock"}
```

The road's walls carry on up into an arch whose crown is `height` metres
(at least 3.5) over the middle of the road; the walls rise straight to 55%
of that and the arch spans the road and its verges. A tunnel always has its
walls, even over a `no_walls` stretch. `style` is `rock`, `masonry`, `ice`,
`concrete`, `lava` or `wreck` (a ship's hull: timber ribs, portholes along
the sides); left out, the theme picks one (snow ice, volcano lava, night
concrete, market and harbor masonry, else rock).

* In the race: the roof stops jumps (a kart bumps off it and comes back
  down) and folds the glider away, so keep glide ramps and big trick ramps
  out of tunnels. Karts aren't dropped back onto the road inside one or
  within 2 m of its ends; a respawn goes back before it.
* In the game: a thick shell outside with a portal at each end, lamps along
  the crown every 14 m (lighting the inside in the style's colour), the
  daylight fading while the camera is inside, the chase camera kept under
  the roof, and the stretch dashed on the minimap.
* Keep tunnels off a branch's first and last `junction` metres, and clear of
  other roads passing within the arch.

Dune Drift's quarry tunnel, Frost Pass's ice cave and Coral Cove's sunken
wreck are branches with tunnels; Canyon Run, Lava Works (a lava tube) and
Neon Raceway (an underpass) have one on the main loop.

## Gates: what changes during a race

A `when` is a gate. Every gate reads only shared race state, so online
clients predict it exactly:

```json
"gates": {
  "final_lap": {"from_lap": 3},
  "drawbridge": {"cycle": [12, 5, 3]},
  "lever": {"switch": "lever"}
}
```

* `{"laps": [2, 3]}`, `{"lap": 2}`, `{"from_lap": 2}`: the leader's lap (on a
  point-to-point track their section; `sections`, `section` and `from_section` are the same).
* `{"cycle": [period, open, offset]}`: seconds of race time; holds for `open` of every `period`.
* `{"switch": "name"}`: while that switch is pressed.
* `"invert": true` flips any of them. A `when` can name a gate (`"when": "lever"`,
  `"!lever"` for its opposite) or write one inline (`"when": {"from_lap": 2}`).

## Branches: alternate paths

```json
"branches": [{
  "name": "orchard_cut", "from": [6, 10], "to": [9, 0], "side": "left",
  "points": [[40, 3, 150, 5], [80, 6, 175, 5]],
  "half_width": 5, "surface": "offroad_light", "junction": 22,
  "when": "lever", "vanish": false, "cpu_share": 0.4,
  "features": [{"type": "boost_pad", "at": 40}]
}]
```

A branch leaves the main road at `from` and rejoins it at `to`, on its left
or right `side`, through its own control points. The builder makes the
split: the branch opens out of the main road like an exit lane over
`junction` metres, then a grass island narrows to a walled nose; the merge is
the same in reverse. Leave room after the junction before the branch turns
hard, and keep its control points on its side of the main road: put its
first point about 20 m past the junction, beside the main road and level
with it, and a second one further on and further out, so it leaves at a
shallow angle (the same in reverse at the merge). Fork and merge on a
straight, not in a bend. `gk_trackmap` prints a FOLD warning for a side road
that bends tighter than it is wide.

* `when`: the branch is open while the gate holds. Shut, a barrier closes its
  mouth; with `"vanish": true` the road itself disappears instead (a bridge
  that comes and goes) and a fence closes the main road's side. A vanishing
  road waits until nobody is on it.
* `cpu_share`: the fraction of CPUs that take it when it's open (they change
  their minds each lap). A CPU that wants a branch opened by a switch drives
  over the switch.
* Progress, checkpoints and placings work on branches; a shortcut counts as
  covering the stretch of main road it bypasses.
