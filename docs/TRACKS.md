# Track files

Every track is a JSON file in `data/tracks/`, named by its id and listed in a
cup in `data/tracks/tracks.json`. `gk_trackmap data/tracks/tracks.json out/ [id] [--race]`
draws each one from above (main road green line, side roads cyan, barriers
orange, switches yellow, zones hatched) and with `--race` runs a 12-CPU race on it.

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
hard, and keep its control points on its side of the main road.

* `when`: the branch is open while the gate holds. Shut, a barrier closes its
  mouth; with `"vanish": true` the road itself disappears instead (a bridge
  that comes and goes) and a fence closes the main road's side. A vanishing
  road waits until nobody is on it.
* `cpu_share`: the fraction of CPUs that take it when it's open (they change
  their minds each lap). A CPU that wants a branch opened by a switch drives
  over the switch.
* Progress, checkpoints and placings work on branches; a shortcut counts as
  covering the stretch of main road it bypasses.
