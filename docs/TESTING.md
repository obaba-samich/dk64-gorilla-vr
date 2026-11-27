# Testing

How each piece was checked, and what still needs a person in a headset.

**Legend:** ✅ checked · 🧪 checked in the simulator only (Monado simulated HMD + synthetic hands, eye
dumps and logs) · ⬜ not checked yet · ❌ known broken / not implemented

## Automated

| Test | How | Status |
|---|---|---|
| Locomotion core: 32 cases (stick, push, launch, cap, history, momentum carried into the next push, climb, walls, slip, ice, two hands, arm reach, water, frozen, reset, landing, head height, game-owned flight) | `tools/test.sh` | ✅ |
| Gestures: 26 cases (adds: no crouch while a hand rows; left-stick flicks for all four actions, one per flick, off when the stick is busy or Tag Anywhere has left / right, still on with gestures off) (idle, slam, punch, head yaw, rowing isn't a punch, punch into a wall, double punch, gun draw at the hip only, fire, orange throw and late release, slow drop, instrument, twirl, duck, climb, left-handed) | `tools/test.sh` | ✅ |
| VR host bridge tests (from astromaddie/dk64-vr), plus the arm-mode stick routing (turn and C-up / C-down on the left stick while the right one does abilities) | `tools/test.sh` | ✅ |
| Core compiles for MIPS with no libc | `clang -target mips ... -nostdinc` in the patch build | ✅ |

## Build and run

| | Status | Notes |
|---|---|---|
| Clean build on Pop!_OS 24.04 with `tools/build.sh` | ✅ | |
| Stock flat game boots and saves | ✅ | 2026-11-24 |
| VR off: no OpenXR, no vr.toml, no `[vr]` log lines | ✅ | flat run after all VR patches |
| `--vr` with no runtime falls back to flat | ✅ | logs `VR mode off` |
| Clean exit from VR (was a segfault) | ✅ | fixed in RecompFrontend patch |
| Testing tab: unlock everything | 🧪 | `diag_unlock`: ran with no crash, logged. Not yet checked in the pause menu by eye |
| Testing tab: warp to all 69 places | 🧪 | `diag_warp_tour`, 2026-11-25: 68 loaded in order with no crash; the tour stops in DK Arcade by design, so Jetpac wasn't reached. A warp waits through cutscenes, pause and arcade games |
| Testing tab renders | 🧪 | panel dump with `diag_vr_menu_tab = 5` |

## Headset path

| | Status | Notes |
|---|---|---|
| OpenXR session, Vulkan device shared with RT64 | 🧪 | Monado, RX 570 |
| Stereo eyes, per-eye asymmetric FOV | 🧪 | eye dumps |
| Menus / title / file select on the panel | 🧪 | from the renderer base |
| Cutscenes in the cinema | 🧪 | intro story dumps |
| SteamVR + ALVR on Quest 3S | ⬜ | needs the user |
| WiVRn on Quest 3S | ⬜ | needs the user |
| Comfort at 72/90/120 Hz | ⬜ | needs the user |
| Touch controller bindings | 🧪 | profile accepted by Monado; buttons not pressed for real |

## Arm locomotion

| | Status | Notes |
|---|---|---|
| Body follows DK, first-person camera | 🧪 | DK Isles |
| Own model hidden | 🧪 | eye dumps |
| Hands drawn in Kong colours | 🧪 | visible at the frame edge in dumps |
| Arms with elbows, hands with fingers and thumb (D37) | 🧪 | eye dumps during the punch routine (2026-11-26) |
| Hands start on the surface after a pause (D40) | ✅ unit test | the old core left them inside the floor, not holding |
| Smack into the ground rises over frames (D40) | ✅ unit test, 🧪 | sim: 5.6 then 3.5 units instead of a 10-unit pop |
| Swimming detected indoors, paddling moves (D40) | 🧪 | Aztec llama temple: `in water (surface 337.4)`, ~90 units of paddling |
| Arm and hand boxes interpolated (D41) | 🧪 | eye dumps during the punch routine |
| Grip climbing (D41) | ✅ unit test | hold, pull up, let go and fall; no tree reached in the sim |
| Hands collect items (D41) | ⬜ | builds and runs; no item reached in the sim |
| Warp sweep with rowing (D41) | 🧪 | 3 runs: 5, 6, 9 and 11 places with arm locomotion driving, 50 throws; 2 crashes (heap: heapFreeDeferred in the resource list, and an animation load) right as a map's intro cutscene began after a warp; also seen once with no warp. Not tied to arm locomotion yet (one run with it off had no crash; too few). A crash in the first headset session predates this work. The crash report is in the log now |
| Tag Anywhere swap under stress (D43) | 🧪 | forced swaps every 15 s during warp tours: 3 of 4 runs crashed before, 0 of 2 after (40 warps, 14 swaps) |
| Trees: the game lets go, grips hold (D44) | 🧪 | the rowing ran into a tree (state 13) and the let-go fired; holding with grips only unit tested |
| Hands into barrels (D39) | ⬜ | the jump starts and the body is held at a barrel; no open barrel in reach on the simulator's saves |
| Left eye mirrored on the monitor (D39) | 🧪 | starts (`mirror: left eye on the desktop window`), clean exit; the window itself not looked at |
| Head moves inside the body (D37) | ✅ unit test | head bobbing leaves the body still; crouching puts the hands on the floor |
| Real floor from the STAGE space, world sized to your eye height (D34) | 🧪 | `floor: tracked, eyes 1.62 m above it`; scale 12.9 units/m for DK |
| Rowing along the floor moves DK | 🧪 | after D34: ~900 units in under a minute of synthetic strokes, 62 throws, 117 contacts. Before D34: ~430 units across Training Grounds in hops, plus the fall off the start ledge (2026-11-25). Before D29 the hands stuck to the start ledge and DK only jittered; the old "~240 units" came from runs with snap turns resetting the hands |
| Falling off ledges uses DK64 gravity | 🧪 | treehouse ledge |
| Throws carry DK (flown by the core, D29) | 🧪 | `launch ... m/s` then `throw over (grabbed)` 10-37 frames later; 19 throws in 2 minutes |
| Hands let go of ledge sides | 🧪 | 123 contacts in 2 minutes instead of 2 (D29) |
| Climbing a ledge / step by pulling down | ⬜ | wall contacts from floor-height steps; not exercised by the synthetic hands |
| Flat walls stop the body | ⬜ | relies on the game's wall pass |
| Swimming by paddling, under water and at the surface | ⬜ | code in, never reached in the sim |
| Slippery surfaces (Caves ice, slides) | ⬜ | surface 0xC slips (D20); not reached in the sim |
| Snap turn in first person | 🧪 | `diag_turn_stick`: turns every 8 s while rowing, no hand drag or false throws |
| Recentre with a hand held | ⬜ | hands reset on a head jump over 35 cm (D25); needs a real recentre |
| Head walking doesn't stop gravity / jumps | ⬜ | fixed by reading the code (D23); only a moving real head shows it |
| Climbing mode (vines, trees, ladders, ledges) | ⬜ | behaviour-based (D24); the sim's rowing never hangs on anything |
| Per-Kong bodies | ⬜ | table in place, only DK tested |
| Haptics on contact | ⬜ | sent; Monado has no haptics |
| Wrist HUD | ⬜ | placed on the off hand's aim pose; the sim's hands have no real pose |
| Immersive cutscenes (default): stereo, look all round, no letterbox bars | 🧪 | intro cutscene, eye dump with the view turned 150°: the level is drawn behind the camera (2026-11-26) |
| Shop cutscenes through the Kong's eyes, right stick click to switch (D36) | 🧪 | warp tour: Cranky's and Funky's start in first person and go back after; eye dumps of Funky's entrance from DK's eyes. The click itself is untested (the sim has no stick click) |
| Cutscene TV room (option), arcade room | ⬜ | layers submitted; Monado's null compositor shows nothing, eye dumps don't include quads |
| Control modes (arms / third person / barrel) | 🧪 | `[vr-loco] mode` lines: arms on DK Isles; the others need the maps |

## Abilities

Every ability works on the buttons as before, and has a gesture (docs/DECISIONS.md D16). The
gestures press the same combos, so a gesture "works" when its buttons arrive; whether the move
happens is the game's call (owned move, ammo, pad).

The sim column: `diag_gestures` runs a scripted routine through the host, and the game logs
`[vr-gesture] events`. Seen in the sim (2026-11-24 and 2026-11-25): punch, double punch (Z+B),
gun draw / holster (Z+C-left), orange (Z+C-right), with snap turns and Kong swaps in the same run. Firing needs a gun, which the test save doesn't have yet.

| Ability | Gesture | Unit test | Sim | Headset |
|---|---|---|---|---|
| Simian Slam | both hands down hard in the air | ✅ | ⬜ | ⬜ |
| Punch | fast jab | ✅ | 🧪 | ⬜ |
| Kong special (Chimpy Charge, Primate Punch, Orangstand...) | double jab, or right stick right | ✅ | 🧪 | ⬜ |
| Guns (all five) | dominant hand grip at hip, or right stick up; trigger fires | ✅ | 🧪 draw (gesture and stick) | ⬜ |
| Oranges | off hand grip at hip, throw, release; or right stick down | ✅ | 🧪 | ⬜ |
| Instruments (all five) | both hands up front, both grips; or right stick left | ✅ | 🧪 stick: the game plays it (state 103) | ⬜ |
| Vines, ladders, trees | grip overhead, pull | ✅ | ⬜ | ⬜ |
| Pony Tail Twirl | arms out wide in the air | ✅ | ⬜ | ⬜ |
| Crouch, pads (Baboon Blast, Spring, Balloon, Monkeyport...) | duck = Z | ✅ | ⬜ | ⬜ |
| Rocketbarrel, Baboon Balloon flight | left stick after 0.8 s in the air | — | ⬜ | ⬜ |
| Tag Barrels | jump in, lean to turn, fling up = A | — | ⬜ | ⬜ |
| Tag Anywhere (experimental, off by default) | right stick flick left / right | — | 🧪 12 forced swaps through all five Kongs, clean exit | ⬜ |

## Playthrough

Not done yet; it needs a headset. The step-by-step list (setup, basics, arms, gestures, Kongs and
barrels, vehicles and minigames, bosses, settings) is kept with the project notes and handed over
with each build. Results go here, per level:

| Level | Arms | Gestures | Bosses / minigames | Notes |
|---|---|---|---|---|
| DK Isles | ⬜ | ⬜ | — | |
| Jungle Japes | ⬜ | ⬜ | Army Dillo, minecart | |
| Angry Aztec | ⬜ | ⬜ | Dogadon, beetle race | |
| Frantic Factory | ⬜ | ⬜ | Mad Jack, car race, DK Arcade, Jetpac | |
| Gloomy Galleon | ⬜ | ⬜ | Pufftoss, seal race | |
| Fungi Forest | ⬜ | ⬜ | Dogadon 2, minecart | |
| Crystal Caves | ⬜ | ⬜ | Army Dillo 2, beetle race, ice | |
| Creepy Castle | ⬜ | ⬜ | King Kut Out, minecart, car race | |
| Hideout Helm | ⬜ | ⬜ | Helm barrels | |
| K. Rool | ⬜ | ⬜ | five phases | |

## Climb and swim checks (headless)
- `diag_climb = 180` (after `diag_gorilla`): each map, the Kong is put by the nearest climbable and
  climbs hand over hand with the grips. Look for `diag climb: y ... holding 1` going up.
- `diag_swim = 150` (after `diag_gorilla`): finds deep water near the start, drops in and
  breaststrokes. Look for `swimming: ... speed` lines. Gloomy Galleon (`diag_warp_first = 6`) works.
