# Plan

Status key: done / done (sim) = works against Monado's simulated headset, not yet worn /
in progress / todo.

| # | Phase | Status | Notes |
|---|-------|--------|-------|
| 1 | Environment, ROM verified, stock build runs flat | done | US 1.0 ROM (sha1 cf806ff2...). Stock v1.0.3 builds with `tools/build.sh`, boots and writes its save. |
| 2 | OpenXR session, flat game on a virtual screen | done (sim) | Session, swapchains, panels and menus on x86_64 + RX 570, built on astromaddie/dk64-vr's host. Needs a real check on Quest 3S through ALVR or WiVRn. |
| 3 | Stereo world rendering, render-time head pose, interpolation | done (sim) | True stereo eye passes, late-latched head pose, RT64 interpolation. Eye dumps checked. Comfort on hardware unverified. |
| 4 | Controller input API, hands drawn, first-person body hiding | done (sim) | dk64vr_hands / dk64vr_body channel, first-person camera, own body hidden, hands as Kong-coloured boxes. |
| 5 | Locomotion core + unit tests | done | `src/vr/loco`, 26 tests, `tools/test.sh`. |
| 6 | Locomotion wired into DK64 with its collision | done (sim) | Rowing, falls, throws, swimming code, slippery surface 0xC. Walls with no top surface only stop the body (D11). |
| 7 | Shared moves: gun, oranges, vines, ladders, Simian Slam | done (sim) | Gestures (D16): slam, gun draw / fire, orange throw, punch, climbing stick. Unit tested; punch and gun draw seen in the sim. Ownership and ammo are the game's. |
| 8 | Per-Kong abilities and instruments | done (sim) | Instrument pose, Kong special (double jab = Z+B), twirl glide (arms out in the air), crouch by ducking for pads. Needs a headset playtest per Kong. |
| 9 | Tag Barrels, per-Kong bodies, Tag Anywhere | partial | Tag Barrels: lean to turn, fling to pick (D18). Per-Kong bodies in place. Tag Anywhere not done (D22). |
| 10 | Vehicles, transformations, minigames, cutscenes, bosses | done (sim) | Mode per situation (D18). Cutscenes / arcades on a TV in a dark room (D19). Bosses run in arm mode except Pufftoss and the K. Rool Diddy phase. |
| 11 | Wrist HUD, menus on the virtual screen, settings/comfort menu | done (sim) | Wrist HUD (D21). All arm locomotion, gesture and room settings in the VR settings menu. |
| 12 | Extras | partial | Contact haptics. Stats overlay, timer and trails not done. |
| 13 | Playthrough checklist | todo | see TESTING.md |
