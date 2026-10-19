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

`points` are the closed loop's control points: x, y (up), z and an optional
road half-width for that point. Widths blend smoothly between points, so a
road can pinch to a narrow bridge and open into a wide plaza.

Positions along the loop are `[control point, metres]`: `[4, 20]` is 20 m
past point 4.

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
| `mover` | a moving platform or block | `at`, `kind`, `travel`, `period`, `phase` |
| `stairs` | main loop only: the road drawn as steps | `from`, `to` |
| `barrier` | a wall that stands while its gate holds | `at`, `when`, `width` (0 = whole road), `offset`, `thickness`, `height` |
| `switch` | a pad; driving over it presses it | `at`, `name`, `offset`, `width`, `hold` (seconds) |
| `zone` | the road's surface changes while its gate holds | `from`, `to`, `surface`, `when`, `side` or `offset` + `width` |
| `tunnel` | a roofed stretch: an arched vault over the road | `from`, `to`, `height` (m, default 7), `style` |
| `bank` | the road tilted across, easing in and out over 15 m | `from`, `to`, `angle` (degrees, + the right side lower, at most 35) |

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

A `tunnel` roofs the road from `from` to `to`, on the main loop or a branch:

```json
{"type": "tunnel", "from": [17, 13], "to": [19, -6], "height": 7.5, "style": "rock"}
```

The road's walls carry on up into an arch whose crown is `height` metres
(at least 3.5) over the middle of the road; the walls rise straight to 55%
of that and the arch spans the road and its verges. A tunnel always has its
walls, even over a `no_walls` stretch. `style` is `rock`, `masonry`, `ice`,
`concrete` or `lava`; left out, the theme picks one (snow ice, volcano lava,
night concrete, market and harbor masonry, else rock).

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

Dune Drift's quarry tunnel and Frost Pass's ice cave are branches with
tunnels; Canyon Run, Lava Works (a lava tube) and Neon Raceway (an underpass)
have one on the main loop.

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

* `{"laps": [2, 3]}`, `{"lap": 2}`, `{"from_lap": 2}`: the leader's lap.
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
