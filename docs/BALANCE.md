# Stat balance: top speed against mini-turbo

How far apart the fastest build and the best mini-turbo build are, and what
changed to bring them together. All numbers come from

    gk_trackmap data/tracks/tracks.json out/ --balance

which runs a Hard CPU alone on each track at 150cc, 3 laps, no items and no
rubber band, five times (seeds 0 to 4: the Hard CPU's lane offset and
look-ahead jitter for grid seats 0 to 4). Best is the fastest single lap over
the 15 laps; mean is all 15, so it includes the standing-start lap and any
wall hits. Gap is the mini-turbo build's mean against the speed build's
(+ = the speed build is faster). Drifting is the share of race time the middle
build spends drifting; at 30% and above a track counts as drift-heavy.
`GK_LAPS=1` prints every lap and every wall hit and respawn.

The builds are real part combinations from `data/parts.json`, chosen by the tool:

| Build | Parts | Speed | Mini-turbo | Accel | Handling | Weight |
| --- | --- | --- | --- | --- | --- | --- |
| Speed | Brute / Hauler / Slick / Wing | 17 | 3 | 4 | 4 | 18 |
| Mini-turbo | Meowie / Pipe Frame / Roller / Parafoil | 3 | 17 | 16 | 14 | 2 |
| Middle | Gobby / Pipe Frame / Slick / Parafoil | 10 | 10 | 11 | 11 | 9 |

## Before

`speed_ground` 19.5 to 27 m/s, `mini_turbo` charge x0.85 to x1.25,
`mt_boost` duration x0.8 to x1.3, `mt_boost_mult` 1.22. Speed build top
speed 25.88 m/s against 20.62 m/s (+25%); the mini-turbo build charges x1.19
and boosts x1.23 as long.

| Track | Speed best | Speed mean | Mini-turbo best | Mini-turbo mean | Middle best | Middle mean | Gap (MT vs speed, mean) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Meadow Circuit | 50.12 | 52.49 | 62.62 | 64.97 | 55.92 | 58.03 | +23.8% |
| Dune Drift | 50.85 | 52.88 | 63.42 | 64.34 | 56.42 | 57.58 | +21.7% |
| Harbor Hop | 45.25 | 46.38 | 57.02 | 57.98 | 50.07 | 51.54 | +25.0% |
| Frost Pass | 52.25 | 54.26 | 64.70 | 65.69 | 57.57 | 59.69 | +21.1% |
| Canyon Run | 41.52 | 45.28 | 49.68 | 52.92 | 46.70 | 48.74 | +16.9% (DNF) |
| Lava Works | 46.92 | 51.18 | 57.63 | 58.84 | 51.60 | 52.69 | +15.0% |
| Cloud Spire | 47.95 | 48.71 | 60.32 | 61.11 | 53.38 | 54.38 | +25.4% |
| Neon Raceway | 46.05 | 48.84 | 56.30 | 57.19 | 50.58 | 51.35 | +17.1% |
| Market Mile | 32.22 | 35.17 | 45.13 | 45.45 | 40.03 | 40.43 | +29.2% |
| Twin Bridges | 39.03 | 41.13 | 47.53 | 48.76 | 42.47 | 44.40 | +18.6% |
| Crane Docks | 41.37 | 45.92 | 51.92 | 53.15 | 45.88 | 48.58 | +15.7% |

The mini-turbo build lost everywhere, by 15% to 29%. Lap time follows top
speed almost one for one: a 25% speed gap was far more than the
mini-turbos could make back.

## After

`speed_ground` (and `speed_water`, which mirrors it) 22.0 to 24.4 m/s,
`mini_turbo` charge x0.85 to x1.8, `mt_boost` duration x0.8 to x2.0,
`mt_boost_mult` 1.25. Speed build top speed 24.04 m/s against 22.36 m/s
(+7.5%); the mini-turbo build charges x1.66 and boosts x1.82 as long.
Acceleration, handling and weight are unchanged: the measurement doesn't
call for them yet. All changed numbers are tagged `provisional` in
`data/physics.json`.

| Track | Speed best | Speed mean | Mini-turbo best | Mini-turbo mean | Middle best | Middle mean | Gap (MT vs speed, mean) | Drifting |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Meadow Circuit | 53.73 | 56.60 | 56.27 | 58.74 | 55.70 | 58.01 | +3.8% | 20% |
| Dune Drift | 54.32 | 56.84 | 57.53 | 58.86 | 55.53 | 56.97 | +3.6% | 31% |
| Harbor Hop | 48.22 | 50.11 | 50.43 | 52.64 | 49.28 | 52.15 | +5.0% | 22% |
| Frost Pass | 55.55 | 57.70 | 57.65 | 58.50 | 56.45 | 58.69 | +1.4% | 37% |
| Canyon Run | 43.28 | 52.00 | 44.72 | 46.32 | 44.50 | 47.23 | -10.9% | 44% |
| Lava Works | 49.45 | 54.38 | 51.07 | 52.64 | 50.37 | 52.12 | -3.2% | 24% |
| Cloud Spire | 51.25 | 52.12 | 54.32 | 55.47 | 52.82 | 53.99 | +6.4% | 27% |
| Neon Raceway | 49.48 | 51.31 | 50.88 | 52.05 | 49.70 | 50.78 | +1.5% | 34% |
| Market Mile | 34.48 | 37.72 | 40.65 | 41.29 | 36.13 | 39.59 | +9.5% | 19% |
| Twin Bridges | 40.80 | 42.94 | 42.97 | 44.27 | 41.47 | 42.98 | +3.1% | 25% |
| Crane Docks | 44.47 | 47.89 | 45.80 | 47.64 | 44.73 | 47.24 | -0.5% | 37% |

On the drift-heavy tracks (Dune Drift, Frost Pass, Neon Raceway, Crane
Docks) the two builds are 0.5% to 3.6% apart. Canyon Run's -10.9% is mostly
the speed build crashing into the pillar at sample ~146 on laps 2 and 3
(see its best laps, 3% apart). Neither build wins everywhere: speed takes
Market Mile and Cloud Spire (long straights, under 30% drifting), mini-turbo
takes Canyon Run, Lava Works and Crane Docks.

Measured before the track reworks; see the end of this file for the numbers
on the reworked tracks.
