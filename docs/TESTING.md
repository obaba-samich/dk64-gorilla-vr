# Testing

How each piece was checked, and what still needs a person in a headset.

**Legend:** ✅ checked · 🧪 checked in the simulator only (Monado simulated HMD + synthetic hands, eye
dumps and logs) · ⬜ not checked yet · ❌ known broken / not implemented

## Automated

| Test | How | Status |
|---|---|---|
| Locomotion core: 26 cases (stick, push, launch, cap, history, climb, walls, slip, ice, two hands, arm reach, water, frozen, reset, landing, head height, game-owned flight) | `tools/test.sh` | ✅ |
| VR host bridge tests (from astromaddie/dk64-vr) | `tools/test.sh` | ✅ |
| Core compiles for MIPS with no libc | `clang -target mips ... -nostdinc` in the patch build | ✅ |

## Build and run

| | Status | Notes |
|---|---|---|
| Clean build on Pop!_OS 24.04 with `tools/build.sh` | ✅ | |
| Stock flat game boots and saves | ✅ | 2026-11-24 |
| VR off: no OpenXR, no vr.toml, no `[vr]` log lines | ✅ | flat run after all VR patches |
| `--vr` with no runtime falls back to flat | ✅ | logs `VR mode off` |
| Clean exit from VR (was a segfault) | ✅ | fixed in RecompFrontend patch |

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
| Rowing along the floor moves DK | 🧪 | ~240 units in two minutes of synthetic strokes |
| Falling off ledges uses DK64 gravity | 🧪 | treehouse ledge |
| Throw → jump with the throw's speed | 🧪 | `jump started, speed set to` log lines |
| Climbing a ledge / step by pulling down | ⬜ | wall contacts from floor-height steps; not exercised by the synthetic hands |
| Flat walls stop the body | ⬜ | relies on the game's wall pass |
| Swimming by paddling | ⬜ | code in, never reached in the sim |
| Slippery surfaces (Caves ice, slides, oil) | ❌ | surface ids are logged (`[vr-loco] hand N on surface S`) but not mapped to slip yet |
| Snap turn in first person | ⬜ | wired through `recomp_vr_take_turn`, not exercised |
| Per-Kong bodies | ⬜ | table in place, only DK tested |
| Haptics on contact | ⬜ | sent; Monado has no haptics |

## Abilities

All of these still work through the buttons (the stick is the only thing taken away; turn on
`stick_move` for moves that need it, like Orangstand Sprint). Gestures are not implemented yet.

| Ability | Buttons | Gesture | Checked |
|---|---|---|---|
| Simian Slam (all levels) | buttons | ❌ | ⬜ |
| Oranges | buttons | ❌ | ⬜ |
| Guns (all five) | buttons | ❌ | ⬜ |
| Instruments (all five) | buttons | ❌ | ⬜ |
| Vines | buttons | ❌ | ⬜ |
| Ladders | buttons | ❌ | ⬜ |
| Baboon Blast, Strong Kong, Gorilla Grab | buttons | ❌ | ⬜ |
| Chimpy Charge, Rocketbarrel, Simian Spring | buttons | ❌ | ⬜ |
| Orangstand, Orangstand Sprint, Baboon Balloon | buttons (+ stick_move) | ❌ | ⬜ |
| Mini Monkey, Pony Tail Twirl, Monkeyport | buttons | ❌ | ⬜ |
| Hunky Chunky, Primate Punch, Gorilla Gone | buttons | ❌ | ⬜ |
| Tag Barrels | A at the barrel | ❌ | ⬜ |

## Playthrough

Not started. Every level, boss, shop, race and minigame still needs a run in the headset, with notes
here.
