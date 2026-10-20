# Items

All 22 MK8 Deluxe race items under our own names (Banana, Green/Red/Spiked
Shell and triples, Bomb, Mushroom x1/x3/Golden, Bullet, Ink, Lightning,
Star, Fireball, Boomerang, Chomper, Horn, Lucky Eight, Coin, Ghost).

## Files

- `src/sim/include/gk/sim/items.h`: `Item` enum, `EntityType` (thrown or
  dropped things: banana, shells, bomb, fireball, boomerang, explosion, oil
  slick), `ItemEntity`, `HitKind` (spin, tumble, shrink), `ItemTuning`
  (every timing and speed), `Rng`, `roll_item(rng, place, karts, blocked)`.
- `src/sim/items.cpp`: roulette weights per place band (provisional, tagged
  in the file), and the `World` members `items_before_step`, `use_item`,
  `throw_item`, `shield_blocks`, `apply_hit`, `items_after_step`,
  `update_entities`. Part of the deterministic step.
- `World::blocked_items` limits Spiked Shell, Lightning and Ink (Blooper) via
  held limits, start gates and cooldowns (`ItemTuning`).
- Hits: `apply_hit` folds a glider; spin and tumble shed speed for
  `spin_shed_frames` / `tumble_shed_frames`, then the kart slides on. It
  records `hit_angle` (from `at`, else the hitting kart) and `hit_speed`
  for the client's animation; on the wire only while knocked.
- Star and bullet: immune to items, bumps, presses and lava zones on the road
  (`kart_step`); the lava floor below and the kill height still take them.

## Rules worth knowing

- Two item slots. Holding the button trails a banana, shell or bomb as a
  shield; Up throws forward, Down throws back. Triples orbit and block one
  hit each.
- Spiked Shell seeks the leader; the Horn is the only defence.
- Tilted road: things on the ground rest and ride by the road's own normal
  (a slope up to 60 degrees, a grip bank at any angle) and keep their speed
  over a change of slope. `ItemEntity::sample` follows each one's spot,
  except for red and spiked shells, where it is their progress along the
  road; those steer in the plane of the road and fly along its up. The
  client stands each item on `sample`'s up. Tests: the grip bank and slope
  cases in `tests/test_mechanics.cpp`.
- Test hooks: `World::give_item`, `spawn_explosion`, `apply_hit`; the client
  has `--give-item N`. Tests: `tests/test_items.cpp`.

## Client side

Item meshes: painted models in `src/client/render/gobby_items.cpp` and
`gobby_items2.cpp` (after Albert's reference paintings), low-poly fallbacks
in `item_mesh.cpp`, HUD icons in `item_icons.cpp`. Held and thrown items are
drawn by `App::draw_held_item` / `App::draw_items` in `src/client/app.cpp`;
effects by `App::event_particles` and `explosion_burst`.
