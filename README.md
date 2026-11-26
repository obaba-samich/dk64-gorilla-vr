# DK64 ReKong VR

Donkey Kong 64 in VR, moving like Gorilla Tag.

This is an **unofficial fork** of [Donkey Kong 64: Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled).
It is not made or endorsed by the DK64 Recompiled team. Please don't take bugs from this fork to
them. The bugs here should only be related to VR.

**Built with AI assistance.** All of the code was built with AI. This was just for fun, I didn't want to put in all of the work to make this a reality. That's why this isn't going upstream, per their [contributing guide](https://github.com/Rainchus/Donkey-Kong-64-Recompiled/blob/main/CONTRIBUTING.md).

**You need your own copy of the game** (US 1.0 ROM, sha1 `cf806ff2603640a748fca5026ded28802f1f4a50`).
Nothing from the game is in this repository.

## Status

Early. Only tested on [Monado's simulated headset](#testing-without-a-headset).
Real testing will be done soon.

| | |
|---|---|
| Flat game, VR off | Same as stock DK64 Recompiled v1.0.3 |
| Stereo rendering, head tracking at display rate | Works (from [astromaddie/dk64-vr](https://github.com/astromaddie/dk64-vr)) |
| Menus, cutscenes, DK Arcade / Jetpac | On a TV in a dark room (cinema room) |
| First person with arm locomotion, climbing, swimming | Works in the simulator |
| Hands drawn, wrist HUD, gesture abilities | Works in the simulator |
| Tag Anywhere (swap Kong with the stick) | Experimental, off by default |

Every ability still works through the buttons (see [Controls](#controls)). What has and hasn't been
checked is in [docs/TESTING.md](docs/TESTING.md); judgement calls are in [docs/DECISIONS.md](docs/DECISIONS.md).

## Requirements

- Linux. VR is Linux only for now.
- A Vulkan GPU. Tested on an RX 570; for that class of card, lower the eye resolution (below).
- An OpenXR runtime with a headset: SteamVR + ALVR, or WiVRn. Made for a Quest 3S, but any
  controller with an OpenXR profile for Touch or Index works.

## Build

```bash
sudo apt install clang lld llvm cmake ninja-build make pkg-config git libsdl2-dev libgtk-3-dev libvulkan-dev glslang-tools libfreetype-dev libx11-dev libxrandr-dev libwayland-dev libxkbcommon-dev libdbus-1-dev
```

```bash
git clone --recurse-submodules https://github.com/obaba-samich/dk64-gorilla-vr.git
```

```bash
cd dk64-gorilla-vr && tools/build.sh "/path/to/Donkey Kong 64 (USA).z64"
```

`tools/build.sh` checks the ROM, makes the decompressed copy the recompiler needs, builds the
matching N64Recomp and a static OpenXR loader into `build-tools/`, applies the renderer patches
and builds `build-cmake/DK64Recompiled`. Run it again with no arguments to rebuild.

## Run

The first time, give it the ROM again so it can copy it into the run folder:

```bash
tools/run.sh "/path/to/Donkey Kong 64 (USA).z64"
```

After that, flat:

```bash
tools/run.sh
```

and VR:

```bash
tools/run.sh --vr
```

`run/` holds the config and saves, so they don't mix with an installed copy of the game.
To make VR the default, put `vr_enabled = true` at the top of `run/vr.toml`.

### Quest 3S with SteamVR + ALVR

1. Install Steam and SteamVR, run SteamVR once.
2. Get the ALVR streamer for Linux from the [ALVR releases](https://github.com/alvr-org/ALVR/releases)
   and the ALVR client on the Quest (same version on both).
3. Start the ALVR dashboard, let it start SteamVR, connect the headset.
4. Make SteamVR the OpenXR runtime: SteamVR settings, Developer, "Set SteamVR as OpenXR runtime".
5. `tools/run.sh --vr`

### Quest 3S with WiVRn

WiVRn is usually smoother than SteamVR on Linux.

1. Install the WiVRn server (`flatpak install flathub io.github.wivrn.wivrn`) and the WiVRn app on
   the Quest, same version.
2. Start the server and connect from the headset. While it runs, WiVRn is the active OpenXR runtime.
3. `tools/run.sh --vr`

If the game says `VR mode off`, it didn't find a runtime. Point it at one directly, for example:

```bash
XR_RUNTIME_JSON=/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json tools/run.sh --vr
```

### Slower GPUs

In `run/vr.toml`, under `[vr]`: `resolution_scale = 1.0` and `refresh_rate = 72`. On an RX 570 also
try `foveation = "off"` (it needs a newer GPU anyway).

## Controls

Moving is all arms: touch the ground or a wall and push or pull. Let go while moving to throw
yourself. The faster the last bit of the stroke, the further you go.

Buttons (Quest Touch):

| Button | Game |
|---|---|
| A | A (jump, talk, confirm) |
| B | B (attack) |
| X / Y | C-left / C-right |
| Grips | gestures (gun, oranges, instrument, climbing) |
| Left trigger | Z |
| Right trigger | R |
| Menu | Start |
| Left stick click | short: recomp menu, hold 1 s: recentre |
| Menu + left stick click, held | VR settings |
| Right stick | left/right: snap turn, up/down: C-up / C-down |
| Left stick | nothing, unless `stick_move` is on; with Tag Anywhere on, flick left/right to swap Kong |

Minecarts, races, Rambi, Enguarde, Pufftoss and the K. Rool Diddy phase switch to the
third-person camera with the stick.

### Gestures

Every one still works on its button too.

| Move | Gesture |
|---|---|
| Punch | fast jab forward |
| Kong special | both hands jab together |
| Simian Slam | both hands down hard while in the air |
| Crouch | duck with both hands free |
| Gun | gun hand to your hip, grip; trigger fires; hip + grip again to put it away |
| Orange | other hand to its hip, grip, throw and let go |
| Instrument | both hands up in front of your face, grip both, hold |
| Pony Tail Twirl | arms spread wide in the air |
| Climb | grip overhead and pull (or the left stick) |

Each one buzzes both hands when it registers.

## Settings

Everything lives in `run/vr.toml` under `[vr]`, and most of it is in the in-game VR settings menu.
The arm locomotion ones:

| Key | Default | |
|---|---|---|
| `gorilla_mode` | true | arm locomotion and first person; false = the third-person VR camera |
| `gorilla_world_scale` | 26 | game units per metre in first person; higher = you're smaller |
| `arm_scale` | 1.15 | real reach multiplier |
| `jump_multiplier` | 1.1 | throw speed multiplier |
| `max_jump_speed` | 6.5 | throw speed cap, metres a second |
| `stick_move` | false | let the left stick move you too |
| `gestures` | true | ability gestures; false = buttons only, grips back to C-down / C-up |
| `dominant_hand` | right | gun and menu pointer hand; the other throws oranges and wears the HUD |
| `duck_crouch` | true | ducking holds Z |
| `tag_anywhere` | false | flick the left stick to swap Kong (experimental) |
| `wrist_hud` | true | HUD as a watch on your off hand |
| `cinema_room` | true | cutscenes and arcade games on a TV in a dark room |

## Testing page

VR settings → Testing: warp to any of 69 places (levels, lobbies, bosses, every K. Rool phase,
races, bonus barrels, shops, DK Arcade, Jetpac), and unlock every Kong, move and item (press
twice; it saves with the file, so use a spare one). No save file is included; a 101% `.eep`
from any N64 emulator can be copied to `run/saves/DK64.bin`.

## Testing without a headset

[Monado](https://monado.freedesktop.org/) has a simulated headset. Build it anywhere, then:

```bash
XR_RUNTIME_JSON=/path/to/monado/share/openxr/1/openxr_monado.json SIMULATED_ENABLE=1 tools/run.sh --vr
```

With `diag_autostart = 150`, `diag_gorilla = 120`, `dump_frame = 600` and `diag_exit_seconds = 200`
in `[vr]` the game plays itself into DK Isles, rows around with fake hands, writes eye images to
`run/vr-dumps/` and quits. `[vr-loco]` lines in the log show where DK went.

Unit tests (no ROM, no headset):

```bash
tools/test.sh
```

## Credits

- [Donkey Kong 64: Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled) by Rainchus,
  Ballaam, Killklli, 2dos, Umedtakes and contributors (GPL-3.0). The game port this is built on.
- [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) and [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) by Wiseguy and contributors.
- [RT64](https://github.com/rt64/rt64) by DarioSamo and contributors (MIT).
- [astromaddie/dk64-vr](https://github.com/astromaddie/dk64-vr) (GPL-3.0): the OpenXR host, stereo
  renderer patches, VR menu and the sfvr helpers come from there, mostly unchanged.
- The [DK64 decompilation](https://gitlab.com/dk64_decomp/dk64) and the
  [DK64 Randomizer](https://github.com/2dos/DK64-Randomizer) for function names and addresses.
- Tag Anywhere's Kong swap is ported from the [DK64 Randomizer](https://github.com/2dos/DK64-Randomizer)'s
  Tag Anywhere by Ballaam, Isotarge and contributors (MIT).
- Ideas from [Dooder64/wave-race-64-vr](https://github.com/Dooder64/wave-race-64-vr),
  [RogueSquadron64Recomp](https://github.com/MikeSemicolonD/RogueSquadron64Recomp) and
  [SkyRecomp-VR](https://github.com/marclemieux64/SkyRecomp-VR).
- Gorilla Tag (Another Axiom) for the way of moving.

GPL-3.0, like the projects it comes from. The upstream README is in
[docs/UPSTREAM_README.md](docs/UPSTREAM_README.md).
