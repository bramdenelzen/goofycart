# Glider flight models for an energy-trade glide

Research for [#17](https://github.com/Mexury/gobbykart/issues/17) (map [#16](https://github.com/Mexury/gobbykart/issues/16), "Physics-based glider").

**Question:** what simple, stable flight model fits an arcade glide where diving buys speed, climbing costs it, ramp momentum carries in, and the glider never stalls?

**Clean-room note:** every Mario Kart claim below comes from plain-language wiki and forum descriptions. No decompiled or datamined code was read.

## Short answer

Don't use a full aerodynamic model, where lift and drag act on an angle of attack. Drive the **flight-path angle** directly from the stick. Then integrate one scalar **airspeed** with two terms: gravity along the path, and a drag that rises with speed squared. Lift never appears as a force. It shows up in two places: drag is tuned so the trim glide is in balance, and the pitch-up authority fades as speed drops. That fading replaces a stall with a soft speed floor.

This is the energy-height idea from real gliding (height and speed are one budget, spent at a rate set by L/D). It is not the phugoid oscillation a free-pitching aircraft shows.

## What the sources say

### Real gliders (physics ground truth)

- A steady glide balances weight, lift (perpendicular to the path) and drag (along it). From the horizontal balance, **L/D = 1/tan(glide angle)**, about 1/angle for small angles. A higher L/D means a flatter glide and more distance per metre of height lost. ([NASA Glenn, vector balance of forces on a glider](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/vector-balance-of-forces-glider))
- Lift and drag both scale with **V²**: L = C_l·ρV²A/2 and D = C_d·ρV²A/2. ([NASA Glenn, lift-to-drag ratio](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/lift-to-drag-ratio))
- A stall depends on angle of attack, not speed. Flying faster means flying at a smaller angle of attack for the same lift. Best glide is flown at the angle of attack with the best L/D. ([British Gliding Association, "What makes a glider fly"](https://www.gliding.co.uk/wp-content/uploads/sites/3/2019/06/What-makes-a-glider-fly.pdf))
- A free aircraft that trades height and speed does it as the **phugoid**: a long, lightly damped cycle of "a repeated exchange of airspeed and altitude" at nearly constant angle of attack. Its period is under 30 s for light aircraft. ([Wikipedia, Phugoid](https://en.wikipedia.org/wiki/Phugoid))
- The full phugoid model as ODEs, with lift and drag folded into a trim speed v_t and an L/D, integrated by explicit Euler ([numerical-mooc, "Phugoid full model"](https://nbviewer.jupyter.org/github/numerical-mooc/numerical-mooc/blob/master/lessons/01_phugoid/01_03_PhugoidFullModel.ipynb)):
  - v' = −g·sinθ − (C_D/C_L)·(g/v_t²)·v²
  - θ' = −(g/v)·cosθ + (g/v_t²)·v

  The first line is the useful one: **gravity along the path, minus a drag of (g/L·D)·(v/v_t)²**. At trim speed in a trim glide the two cancel. The second line is the part arcade games drop: it makes the path angle oscillate (the phugoid) and climb on its own at speed. That is the "kite on a string" porpoising that a kart glider should not have.

### Mario Kart (plain-language descriptions only)

- A glider deploys when a racer drives over a **Glide Ramp** ("a blue panel, usually at the end of ramps") and lets them float across gaps or over part of the track. Touching the ground ends it. In MK7, braking or an item hit retracts it. In MK8 and MK8 Deluxe it cannot be retracted. ([Super Mario Wiki, Gliding](https://www.mariowiki.com/Gliding); [Mario Kart Racing Wiki, Glider](https://mariokart.fandom.com/wiki/Glider))
- Pitch: steering works as normal and **"up and down are inverted"**: stick back is nose up. ([Mario Kart Racing Wiki, Glider](https://mariokart.fandom.com/wiki/Glider))
- Stats: the glider part shifts weight and acceleration slightly, and also the **hidden air speed and air handling stats**. ([Super Mario Wiki, Gliding](https://www.mariowiki.com/Gliding)) The MK8DX stats page says air speed sets maximum speed in the air and is not affected by coins. Its column labels for air and anti-gravity contradict each other. ([Super Mario Wiki, MK8DX in-game statistics](https://www.mariowiki.com/Mario_Kart_8_Deluxe_in-game_statistics))
- Pitch vs speed in MK8 (**forum, low trust**): one player says nose-down "increases speed significantly". A reply says "in 8 there is no speed boost from nose diving, it simply gets you to the ground faster". Others agree that diving is slower overall because the path is longer, and that landing early is worth it only to get drifts and mini-turbos back. ([GameFAQs MK8 board](https://gamefaqs.gamespot.com/boards/700050-mario-kart-8/69863995))
- No primary Nintendo source describes the glide physics.

**Reading:** MK8's glide looks like a near-constant air speed set by the stat, with pitch steering mostly the sink rate. That matches our *current* glider: [kart.cpp:219-241](../../src/sim/kart.cpp) eases horizontal speed toward `glide_speed·(1 − 0.18·pitch)`. #16 asks for more than this, a real energy trade, so MK8 is the floor to beat, not the target.

## Candidate model: path angle plus energy

State while gliding: airspeed `V` (scalar), the sine of the path angle `s = sinγ` (positive is up), heading `dir` (horizontal unit vector), and the bank.

Per tick (`dt = 1/60`):

```
// 1. Stick to target path angle. Pitch authority fades as speed drops (lift ~ V²).
lift_frac = clamp((V² − V_floor²) / (V_trim² − V_floor²), 0, 1)     // 0 at the floor, 1 at trim and above
s_trim    = −1 / sqrt(1 + LD²)                                       // sin of the trim glide angle
s_up      = s_trim + climb_range * lift_frac                         // nose-up limit shrinks as speed bleeds
s_down    = s_trim − dive_range
s_target  = pitch >= 0 ? lerp(s_trim, s_up, pitch) : lerp(s_trim, s_down, −pitch)
s_target  = min(s_target, s_trim + (V − V_floor) * floor_gain)       // below trim near the floor: nose drops itself
s         = approach(s, s_target, path_rate * dt)

// 2. Energy: gravity along the path minus V² drag tuned so trim is in balance.
drag      = (g / LD) * (V / V_trim)² * (1 + bank_drag * bank²)
V        += (−g * s − drag) * dt
V         = clamp(V, V_floor, V_cap)                                 // V_cap = boost speed (decided)

// 3. Velocity (no trig: c = sqrt(1 − s²)).
air_vel   = dir * (V * sqrt(1 − s*s)) + up * (V * s)
```

Why each part:

- **Lift scaling with speed.** Lift is never a force. It appears as `lift_frac`: how much nose-up the wing can "hold" scales with V², as real lift does. At trim speed and above, the stick has full authority. As V bleeds toward the floor, the climb limit shrinks back to the trim glide, so you cannot pull up into a stall.
- **Drag.** `(g/LD)·(V/V_trim)²` is the numerical-mooc drag term. At `V_trim` in the trim glide, gravity along the path (`g·|s_trim|` ≈ g/LD) cancels it exactly, so a hands-off glider settles at the stat speed and the stat's glide ratio. Above trim the drag rises faster than the gravity pull, so speed decays back on its own. That makes the model self-stable with no hidden target speed.
- **Dive into speed.** A steeper `s` gives a larger `−g·s`, which accelerates the glider. Terminal speed in a held dive is `V_trim·sqrt(LD·|s|)`, which is usually above the cap, so the cap is what bites. Climbing reverses it: speed falls, and height is gained at roughly the rate the energy budget allows.
- **Soft floor instead of a stall.** Two layers. First, the fading climb limit and the automatic nose-drop near the floor (`floor_gain`) mean that holding full back stick at low speed gives a nose-down settle that regains speed: a "mush", not a stall. Second, a hard clamp at `V_floor` is a last guard. It should rarely bite, so any energy it adds stays negligible.
- **Ramp momentum carries in.** Seed the state from the launch velocity: `V = |air_vel|`, `s = air_vel.y / V`. A boosted kart starts above trim, and the excess bleeds off through the V² drag over several seconds, not in one ease. The existing `glide_launch_velocity` (7 m/s up) becomes an initial path angle of about +0.28 rad, which `path_rate` bends down into the glide: a natural launch arc.
- **Cap.** `V_cap` is the boost speed (decided on #16). Clamping speed sheds energy above the cap. That is fine for an arcade game, and it is what stops a long dive from beating a boosted ground line.
- **Bank.** A real banked turn needs extra lift (load factor 1/cos(bank)), which costs drag. `bank_drag·bank²` charges a turn in speed rather than adding sink by hand as the current `1 + 0.15·|bank|` does. Keep the existing yaw from `turn_rate_air`. The real coordinated-turn rate `g·tan(bank)/V` would make turns tighter at low speed, which is optional flavour.

### Why not the full phugoid model

Letting the path angle respond to lift (θ' = (g/V)(V²/v_t² − cosθ)) makes a fast glider climb by itself and porpoise with a period of several seconds. It is realistic but reads as unstable and fights the stick. Commanding the path angle with a rate limit (`path_rate`) is the arcade simplification: the player is the pitch damper.

## Parameter ranges (60 Hz)

| Parameter | Suggested | Range | Notes |
|---|---|---|---|
| `V_trim` | `glide_speed` (24 m/s) | stat-driven | the existing SpeedAir stat × cc scale, so part stats keep their meaning |
| `LD` (glide ratio) | 6 | 5–8 | 6 gives a trim sink of 24/6 ≈ 4 m/s, the same as today's `glide_sink` |
| `s_trim` | −0.164 | from LD | ≈ −9.5° |
| `dive_range` | 0.37 | 0.3–0.5 | full dive at s ≈ −0.53 (≈ −32°); accelerates at ≈ 3.6 m/s² from trim, so 24→31.7 m/s in about 2.5 s |
| `climb_range` | 0.37 | 0.25–0.45 | full climb at s ≈ +0.21; decelerates at ≈ 3.7 m/s², so trim→floor in about 3 s; pairs with today's 0.38 rad pitch limit |
| `path_rate` | 0.8 /s (in sin units) | 0.5–1.5 | today's pitch eases at 1.6·0.38 ≈ 0.6 rad/s; higher is twitchier |
| `V_floor` | 0.6·V_trim (≈ 14 m/s) | 0.5–0.75·V_trim | lower makes for longer, floatier climbs |
| `floor_gain` | 0.05 per m/s | 0.02–0.1 | how hard the nose drops itself near the floor |
| `V_cap` | boost speed: `top_speed·boost_pad_mult` ≈ 31.7 m/s | decided | or the mini-turbo multiplier, whichever #16 picks as "boost speed" |
| `bank_drag` | 0.25 | 0.1–0.4 | at full bank (0.65 rad) drag rises ≈ 10% |
| `g` | `k.gravity` | — | keep one gravity constant |

Sanity checks with the defaults:

- **Hands off at trim:** V stays at 24 m/s, sink is about 3.9 m/s, and the 62 m test gap (`track_builder.cpp:886`) is crossed in about 2.6 s, the same as today.
- **Boosted launch at 31.7 m/s:** drag excess is (g/6)·(1.745 − 1) ≈ 1.2 m/s², so the boost carries for several seconds. That is momentum carrying into the glide.
- **Stability at 60 Hz:** the speed's relaxation time is about V_trim·LD/(2g) ≈ 7 s, far longer than dt = 16.7 ms. Explicit or semi-implicit Euler is unconditionally comfortable here. Update `V` before building `air_vel`, as the current code does.

## Determinism notes

- The model needs only `+ − × ÷`, `sqrt` and `clamp`. Because `s = sinγ` is the state, no `sin`, `cos` or `atan2` runs per tick. IEEE-754 requires correctly rounded `sqrt`, unlike libm's `sin` and `cos`, which can differ across platforms. That matters if ghosts and online lockstep must replay bit-for-bit. The sim already calls `std::sin` in track building and `world.cpp`, so this is a choice, not a fixed rule.
- Keep everything as `float` with a fixed tick order, and seed from `air_vel` exactly once, at the glide-ramp launch (`kart.cpp:162`), so only glide ramps start a glide (decided).

## Open questions for #16

- Should "boost speed" for the cap be the boost-pad speed (×1.32) or the mini-turbo speed (×1.22)?
- Should the air stats scale `LD` as well as `V_trim`? That would let gliders differ in float as well as speed, closer to MK8's separate "air speed" and "air handling".
- Is the coordinated-turn rate `g·tan(bank)/V` wanted, or should the turn rate stay a fixed stat?
