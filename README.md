# VoidOS-New

Early-stage VoidOS simulator bootstrap: the first working slice of Stage 1 in
[PLAN.md](PLAN.md) — a fitness-first, independent PineTime watch paired with
gym-void. This repository contains a portable C11 core plus an SDL2 desktop
simulator. **There is no hardware firmware here yet**, no BLE, no persistence,
and no real sensor data.

**SIMULATOR / NO SAVE**: everything the simulator shows (clock, battery, steps,
heart rate, workouts, sleep, water) is simulated in memory and lost on exit.
Nothing is written to disk except optional screenshots.

## Requirements

- macOS with clang
- SDL2 and SDL2_ttf: `brew install sdl2 sdl2_ttf`

## Build, test, run

```sh
make            # builds build/voidos-sim
make test       # builds and runs native core tests (mock HAL, no SDL needed)
./build/voidos-sim
make clean      # removes built binaries only; keeps screenshots/artifacts
```

Headless smoke test (software renderer, quits after N rendered frames):

```sh
SDL_VIDEODRIVER=dummy ./build/voidos-sim --frames 2
./build/voidos-sim --frames 5 --screenshot out.bmp   # 240x240 BMP of last frame
./build/voidos-sim --screen stats --frames 5 --screenshot stats.bmp
# --screen: face | stats | controls | notifications | nutrition
```

## Controls

| Input | Watch equivalent |
| --- | --- |
| Left mouse press / drag / release | Touch down / move / up (240x240 logical, window is 2x) |
| `B` or `Space` | Side button short press (back one level / home) |
| `Esc` or window close | Quit |
| Arrow keys | Inject equivalent swipe (Up/Down/Left/Right) |

Gestures in the core: a release within 12 px of the press point is a tap, but
only if the finger never moved further than that during the stroke; an
axis-dominant drag of 40 px or more is a swipe; anything in between is ignored
and cannot trigger buttons.

## Implemented flows

- **Watch face**: full-bleed analogue dial for the square 240x240 panel —
  minute ticks stop short of twelve unobstructed numerals in the chosen
  **Jupiteroid** typeface (see `assets/fonts/MANIFEST.md`: CC0) at a uniform
  19 px around a ~94 px ring. A white hour hand, orange minute hand and
  teal seconds hand follow the host clock. The red heart and `-- BPM` move
  opposite the hour hand: above the centre from 3:00–9:00, below from
  9:01–2:59. Battery icon and percentage are in control centre, not the
  watch face. Navigation:
  swipe **down** = control centre, **up** = stats, **right** =
  notifications, **left** = nutrition; tapping the face or pressing the
  side button opens the app launcher.
- **Control centre** (swipe down): battery icon and `87% SIM`, brightness, DND,
  BLUETOOTH NOT CONNECTED, and a tappable Settings entry (Settings opened
  here returns to control centre; opened from the launcher it returns
  there).
- **Stats** (swipe up): feet icon + steps `4321 SIM`, heart icon + HR
  `-- BPM`, sleep `-- NO DATA`, HRV `-- NOT VERIFIED`, workout state
  (live sets or `-- NO SAVE`). No invented sensor measurements.
- **Notifications** (swipe right): `BLUETOOTH / PHONE NOT CONNECTED`,
  empty inbox.
- **Nutrition** (swipe left): `FITVOID / PHONE NOT CONNECTED`, local water
  display with +250 ml tap, food/protein/calories `-- NOT SYNCED`.
- **Launcher**: tappable Workout, Sleep, Water, Stats, Settings, Clock.
  On-screen `[<]` back button plus the side button on every nested screen.
- **Workout**: quick session with 3 set markers, complete one set at a time,
  pause/resume, a 90-second rest countdown between sets, FINISH -> session
  summary. Pausing freezes both the elapsed time and the rest countdown and
  blocks set completion until resumed. Leaving a live session via the side
  button returns to the launcher with the workout still active; tapping
  Workout re-enters it with sets and rest preserved. No routine sync or
  exercise library yet.
- **Sleep**: manual bedtime start/stop producing a TIME IN BED summary only.
  Deliberately no estimated sleep stages — see PLAN.md honesty notes.
- **Water**: +250 ml / +500 ml counters for the session.
- **Settings / clock**: visible honest stubs marked SIMULATED or
  NOT CONNECTED, not dead buttons.

## Layout

```text
include/voidos/   public HAL contract (hal.h) and app API (app.h)
core/             portable C11 state machine, navigation, renderer, bitmap font
simulator/        SDL2 backend (sdl_hal.c) and CLI entry point (main.c)
tests/            native unit tests with a mocked HAL
Makefile          all / test / clean via sdl2-config
```

Core code has no SDL includes, no heap allocation, and no full-screen
framebuffer — it issues clipped `hal_draw_rect` calls only, matching the
nRF52832 RAM budget in PLAN.md. The simulator owns SDL init, a 480x480 window
with 240x240 logical rendering (2x), RGB565-to-SDL color conversion, software
renderer fallback for headless runs, and `--frames`/`--screenshot` for
deterministic smoke tests.

## Current limitations vs the plan

- No persistence, alarms, BLE, phone sync, or real sensors of any kind.
- No record/replay scenario system or screenshot baseline suite yet.
- Sleep captures time-in-bed only; automatic detection and staging are later,
  gated on real-watch validation.
- Workout is a fixed 3-set quick session, not the routine/set model in PLAN.md.
- This slice does not claim the full Stage 1 exit gate.
