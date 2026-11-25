# Plan

Status key: done / done (sim) = works against Monado's simulated headset, not yet worn /
in progress / todo.

| # | Phase | Status | Notes |
|---|-------|--------|-------|
| 1 | Environment, ROM verified, stock build runs flat | done | US 1.0 ROM (sha1 cf806ff2...). Stock v1.0.3 builds with `tools/build.sh`, boots and writes its save. |
| 2 | OpenXR session, flat game on a virtual screen | done (sim) | Session, swapchains, panels and menus on x86_64 + RX 570, built on astromaddie/dk64-vr's host. Needs a real check on Quest 3S through ALVR or WiVRn. |
| 3 | Stereo world rendering, render-time head pose, interpolation | done (sim) | True stereo eye passes, late-latched head pose, RT64 interpolation. Eye dumps checked. Comfort on hardware unverified. |
| 4 | Controller input API, hands drawn, first-person body hiding | in progress | dk64vr_hands / dk64vr_body channel, first-person camera, own body hidden. Hand models not drawn yet. |
| 5 | Locomotion core + unit tests | done | `src/vr/loco`, 26 tests, `tools/test.sh`. |
| 6 | Locomotion wired into DK64 with its collision | in progress | Rowing on floors, falls and throws checked in the sim with synthetic hands on DK Isles. Walls with no top surface only stop the body. Swimming not wired yet. |
| 7 | Shared moves: gun, oranges, vines, ladders, Simian Slam | todo | |
| 8 | Per-Kong abilities and instruments | todo | |
| 9 | Tag Barrels, per-Kong bodies, Tag Anywhere | in progress | Per-Kong body table in place (`vr_kong_bodies`). |
| 10 | Vehicles, transformations, minigames, cutscenes, bosses | todo | Cutscenes, menus, arcade already go to the cinema / panel (from the renderer base). |
| 11 | Wrist HUD, menus on the virtual screen, settings/comfort menu | todo | Panel menus and the VR settings menu exist; no wrist HUD yet. |
| 12 | Extras | todo | Contact haptics done. |
| 13 | Playthrough checklist | todo | see TESTING.md |
