# Decisions

Every judgement call, newest at the bottom. Each one says what was picked and why.

### D1. Base on DK64 Recompiled v1.0.3
Latest release tag of Rainchus/Donkey-Kong-64-Recompiled at the time of writing. Releases are what
people actually run, and mods target them.

### D2. Build with clang 18 pointed at gcc 13's libstdc++
Pop!_OS 24.04 has gcc 13 and 14 under /usr/lib/gcc, but only 13 has libstdc++ headers. clang picks
14 and fails to link. `tools/build.sh` passes `--gcc-install-dir` for the newest gcc that has them,
so nobody has to install libstdc++-14-dev.

### D3. N64Recomp pinned to the runtime's submodule commit
N64Recomp main (Nov 2026) writes section tables with `std::nullopt` fields that the N64ModernRuntime
pinned by v1.0.3 doesn't have. The runtime carries N64Recomp as a submodule; the build script uses
exactly that commit.

### D4. Test runs use a portable folder
`tools/run.sh` runs from `run/` with `portable.txt`, so config and saves stay in the repo folder and
never mix with an installed copy.

### D5. Build on astromaddie/dk64-vr's renderer instead of writing one
That fork (GPL-3.0) already does DK64 stereo through RT64 + OpenXR: eye passes, late-latched head
pose, interpolation, panels for menus, a cinema for cutscenes, comfort turning, a VR menu. Its
RT64 and Plume changes are public patch files. Rewriting that would cost weeks and land in the same
place. Imported in separate commits with credit; this fork adds the x86_64/Quest port, first person
and the arm locomotion on top.

### D6. OpenXR loader built from source, statically
Ubuntu 24.04's libopenxr-dev is 1.0.20; the host needs 1.1 headers. `tools/build.sh` builds
OpenXR-SDK release-1.1.63 into build-tools/ (no sudo). The distro loader is the fallback.

### D7. VR is opt-in at runtime
The VR host only starts with `--vr`, `DK64_VR=1` or `vr_enabled = true` in vr.toml. Otherwise
OpenXR is never touched and the game is the stock flat one.

### D8. Test without a headset using Monado's simulated HMD
No OpenXR runtime is installed on the dev machine, and nobody can wear a headset during
development here. Monado built locally (no sudo, simulated driver) runs the whole session; the
host's diagnostics (autostart, eye dumps, synthetic hands, self-exit) make unattended runs possible.

### D9. Locomotion runs in the game patches, at the game's rate
The patches can call DK64's own collision functions; the host can't. So the core (pure C) is
compiled into the MIPS patches and stepped once per game frame (30 Hz). The camera doesn't wait on
that: the host interpolates the published body at display rate and adds the live head pose.

### D10. Taking over the player in the movement step
`func_global_asm_806651FC` is DK64's generic "move an actor by its speed" step. Patched so that,
for the player while a hand holds, the move is the hands' displacement. Everything around it
(state machine, wall push-out, floors, loading zones, triggers, cutscenes) runs untouched. With no
hand holding, DK64's own gravity, slopes and landing take over, so falling feels like the game.

### D11. Collision: DK64's floor query, plus height jumps as walls
The decomp exposes a clean "highest floor under x,z below y" query (`func_global_asm_8066635C`, the
one the camera uses) and its surface/water results. Wall triangles are only reachable through
unmatched assembly. So hand sweeps use the floor query: dipping under a floor is a floor contact
(normal from neighbouring samples), and moving sideways into a spot whose floor top is above the
hand is a wall contact (steps, ledges, boxes, platforms, cliff tops). Room walls with nothing on
top are only felt by the body, through the game's own wall pass. A real wall query is in FUTURE.md.

### D12. Releases go through the game's jump
A throw with a real upward part presses A for 0.1 s so the game starts its own jump (animation,
sound, state), then the patch swaps the jump's speed for the throw's. Flat throws go straight into
DK's ground speed and the game slows them down like friction. Speeds are converted with the
movement step's own factor (scale_y / 6 per frame).

### D13. Hide the player by collapsing his model matrix
`renderActor` only builds the model matrix. Scaling its 3x3 part to ~0 for the player in first
person hides him without touching any gameplay flag.

### D14. Bigger world scale in first person
`gorilla_world_scale` (26 units/m) is separate from the third-person `world_scale` (15). At 26 DK's
eyes are about 0.8 m off the floor: you feel small and reach the ground without kneeling, which is
the Gorilla Tag feel. Per-Kong sizes (eye height, radius, arm and launch factors) live in
`vr_kong_bodies` in patches/vr_gorilla.c and scale with the model (Mini Monkey, Hunky Chunky).

### D15. Base patches use RECOMP_PATCH, not hooks
`RECOMP_HOOK` is a mod-loader feature; this repo's patches are the base game's own and only
support whole-function patches. The VR patches copy the decomp's C for the few functions they
change (movement step, renderActor) and add calls inside functions the recomp already patches.
All the game-side VR code sits in patches/vr_*.c, so splitting it into a .nrm later is possible.

### D16. Gestures press the game's own buttons
Gestures (src/vr/loco/gesture.c) never start moves themselves. They turn hand motion into the N64
combos the decomp's input tables already listen for (Z in the air = slam, Z held + C-left = gun,
Z + C-right = orange, Z + C-up = instrument, Z + B = special, B = punch / fire). The game still
decides whether the Kong owns the move, has ammo, stands on the right pad. The buttons keep
working the whole time, so every ability has a button fallback.

### D17. Grips belong to the gestures
With gestures on, the grips are the "hand closed" signal (gun at the hip, orange, instrument,
climbing) and no longer press C-up / C-down. Those moved to right stick up / down, which was
unused in first person. With gestures off, the old mapping comes back.

### D18. A control mode per situation
- Normal play as a Kong: arms.
- Rambi, Enguarde, minecarts, beetle / car / seal races, Pufftoss and the K. Rool Diddy phase
  (Rocketbarrel flight in a fixed arena): the third-person VR camera with the stick. Arms can't
  steer a cart or a rhino in any way that feels right, and these are timed.
- The game's own first-person views (scope, Kremling Kosh, Big Bug Bash, Peril Path Panic): head
  aim from the base port, stick on. Locomotion is off there.
- Tag Barrels: the barrel turns on stick X (states 7 / 0xA / 0xB), so leaning your body left or
  right turns it, and flinging both hands up presses A. The barrel is entered by jumping into it,
  which arm throws already do.
- More than 0.8 s in the air with no hand on anything (Rocketbarrel, Baboon Balloon, the twirl
  glide): the left stick steers, since the game owns that flight.

### D19. Cutscenes and arcades on a TV in a dark room
The cinema screen already sat in black (the projection layer only covers the screen). It now has
a bezel and a dim room of flat quads around it, so it reads as a TV in a box rather than a
floating rectangle. The quads are submitted under the projection layer and none overlaps the
picture, so runtimes that sort quads by depth (SteamVR) can't put a wall over it. DK Arcade and
Jetpac use the same room around the floating panel. `cinema_room` turns it off.

### D20. Slippery floors from the surface id
Surface 0xC is the game's own slippery floor: it gets the slide sound and keeps a slide going
(decomp playerStateEnterSlideBrake / playerUpdateSlopeSlidePhysics). Hands slide on it with a
slip of 0.85 (some grip left, so you can still claw along ice). Other surfaces grip fully.

### D21. Wrist HUD on the off hand
In arm locomotion the HUD (bananas, coins, health) is drawn as a watch face on the back of the
non-dominant wrist, 16 cm wide by default, so it never blocks the view and a glance shows it.
Third person and the cinema keep their own HUD placement.

### D22. No Tag Anywhere yet
The game's swap (`playerSetKongByActorType`) works on the "current actor" globals and allocates new
model buffers each time; it has to run inside the player's own update with the right checks
(Kong freed, on the ground, nothing held, no cutscene). Calling it from the frame hook risks a
broken actor or a bad save, so it isn't in this pass. Tag Barrels work. FUTURE.md has the plan.

### D23. Head walking adds to the game's move, it doesn't replace it
With no hand holding on, the tracked head's room-scale motion used to replace the whole movement
step, which also skipped gravity, jump speed and throws whenever the head moved at all (always,
in a real headset; the simulator's head is still, so it never showed). Now the head's motion is
added and the game's own move runs after it. Only a holding or paddling hand replaces the move.

### D24. Climbing found by behaviour, not by state numbers
The newer DK64 decomp names its player state table, but the names don't hold up: state 0x0C,
called "climb tree" there, is the plain movement state (the recomp's own decomp sets it after a
Kong swap, after Mini Monkey ends and after the Ice Tomato game, each followed by "resume
movement"), and 0x1E ("fall damage land") is ordinary falling. So no state lists from those names.
- **Climbing** (vine, rope, tree, ladder, ledge hang): off the ground, no vertical speed, no hand
  held, for 6 frames. First person stays, the arms stop driving and gripping, and grip-overhead
  hand-over-hand (or the left stick) climbs.
- **Scripted moves** (Baboon Blast flights and the like) are left to the game: with D23 the arms
  only replace the move while a hand holds on.
- **Swimming**: the head under the water, or the body in it with the head just above (surface
  swimming, where paddling only moves along the water).
- **Tag Barrel**: states 7 / 0xA / 0xB come from the barrel's own input handler (stick X turns
  7 into 0xA / 0xB), so leaning stays.
- Tag Anywhere only runs in state 0x0C.

### D25. Recentre and snap turn start the hands fresh
Both move every hand position at once. A holding hand would drag the body, and the jump looked
like a throw. The locomotion core is reset on a turn or a head jump of more than 35 cm.

### D26. Tag Anywhere, experimental and off by default
Ported from the DK64 Randomizer's own (MIT): hand models, HUD Kong, the character swap, resume
movement. It runs only in plain standing play: on the ground, normal state, no cutscene, no gun
out, nothing held, normal size, no Strong Kong / sprint / Gorilla Gone, not in Helm, K. Rool,
Troff 'n' Scoff or vehicle maps, and only to freed Kongs (the pause menu's flags 6 / 0x46 / 0x42 /
0x75). Trigger: flick the left stick left or right (unused in arm mode). This replaces D22.

### D27. A controls page written for the Quest, shown once
The VR menu's controller map showed the third-person Xbox layout. With arm locomotion and
gestures on it now lists the Quest buttons by name and every gesture. The first time arm
locomotion takes over, the VR menu opens on that page once (`arms_intro_shown`).

### D28. A Testing tab instead of a shipped save file
Testing every part of the game needs every Kong, every move and a way to any map. A 101% save
file would be data out of the game's save format and someone's playthrough, and it would need a
download, so there's none in the repo. Upstream's debug warp menu (R + D-pad up) needs a D-pad,
which Quest controllers lack. The VR menu gets a Testing tab instead:
- **Place** + **Warp there**: 69 places (hub, levels, lobbies, rooms with water or vines, animal
  buddy arenas, vehicles and races, bosses, every K. Rool phase, bonus barrels, shops, DK Arcade
  and Jetpac), each the game's own map transition to the map's default exit.
- **Unlock everything** (asks for a second press): the freed-Kong and training flags, all
  Cranky / Funky / Candy upgrades for every Kong, full ammo, oranges, crystals, film, three
  melons. Keys, Golden Bananas and blueprints stay as they are, so the file's progress isn't
  rewritten. It saves with the file the next time the game saves.
- Requests are counters the game acts on once each, at upstream's warp-menu point in the frame,
  and wait while there's no player (loading, title, the arcade games).

### D29. Throws are flown by the arm physics
The first headset test: "I could barely move". Two causes, both fixed:
- A hand that touched the side of a step or ledge never let go. The collision sweep gets a
  side's normal from the way the sphere moved, so from inside a side every move read as "further
  in". A sweep that starts inside a side now moves out freely. In the simulator the hands had
  been stuck at the start ledge in every run since the first day, which hid the next problem.
- A release only handed the game a hint: a flat throw set the game's run speed, which it brakes
  to zero with the stick centred; an upward one became the game's jump with our speed swapped in.
  Now the locomotion core flies the body after a release (momentum, gravity at 9.8 m/s² times the
  world scale, ground friction, its sweeps against floors and steps) and the game's own wall pass
  takes away the speed that went into a wall. The flight ends on landing slower than 0.35 m/s, a
  hand grabbing something, water, climbing, a warp, or the game moving the player into a state
  that isn't the throw's (a hit, a slam, a vine, a barrel). The game still gets an A press for a
  throw upwards off the ground, for the jump animation and sound, and its speed fields are
  mirrored so animations and landings follow. This replaces the "game owns free flight" choice.

### D30. Momentum carries, like Gorilla Tag
Gorilla Tag (Another Axiom's GorillaLocomotion, MIT) measures the body's velocity every frame,
whatever moved it, and sets the launch from the average of the last few frames times the jump
multiplier, capped. So pushing off again while already flying keeps that speed and adds to it.
The core's history used to hold only the frames a hand was pulling, and was cleared on release.
It now follows the body through the air too (including the game's moves between steps), and a
release uses the steps up to the last one that held on. Defaults stay at Gorilla Tag's: 1.1x,
6.5 m/s cap.

### D31. Arms are drawn
Each hand gets an arm: a fur-coloured beam from a shoulder point (22 cm below and 17 cm beside
the head, facing where the head faces) to the hand, stuck hands included. Boxes, like the hands,
depth tested against the world.

### D32. The left stick does the abilities too
Gestures for everything was too much. The left stick is free in arm mode, so a flick does one
action: up = gun out / away, down = throw an orange (where you look), left = instrument, right =
Kong special. They press the same combos as the gestures (Z + C-left, Z + C-right, Z + C-up,
Z + B), work with ability gestures off, and are off while the stick moves you (`stick_move`),
steers a long fall or climbs. With Tag Anywhere on, left / right stay its Kong swap. Punches,
the slam, the twirl and ducking stay gestures. `diag_stick_actions` flicks through all four for
the simulator.

### D33. The ability flicks are on the right stick
Asked for after D32. In arm locomotion, while the flicks are on (not `stick_move`, not climbing
or steering a long fall), the right stick does them: up = gun, down = orange, left = instrument,
right = Kong special (Tag Anywhere's swap when that's on). Turning and C-up / C-down move to the
left stick, which moves nothing in arm mode. Everywhere else (third person, barrels, climbing,
long falls, `stick_move`) the right stick turns as before and the left stick moves. The host and
the game use the same condition (body active, arm mode, stick not free, `stick_move` off).

### D34. The real floor is the game's floor (Gorilla Tag's rig, 1:1)
Second headset test: "can't fly up or move forward much, everything below me jitters". The play
space's origin is the head at the last recentre, and the game's floor sat one Kong height under
it: 21 units, which at the old 26 units/m is 0.8 m. Relaxed hands touched the ground at waist
height, every throw was caught within frames, and strokes could only be short. GorillaLocomotion
is a 1:1 rig on the real floor, so now:
- The host locates the head in the runtime's STAGE space (floor at y = 0) and sends where the
  floor is (`standing_eye_height`, 1.55 m, when a runtime has no floor, or seated).
- The game puts its floor there. With `match_height` on (default) the world is sized so your
  eyes are at the Kong's eye height: DK at 21 units and eyes at 1.6 m is about 13 units/m. The
  camera uses the scale the game publishes. Off: `gorilla_world_scale` as before, with the
  floor still at your real floor.
- The prefab's numbers: arm reach 1.5 m, 0.4 m/s before a push throws you, 1 m before a stuck
  hand lets go, 8 history frames at ~90 Hz (3 at the game's 30 Hz), 1.1x, 6.5 m/s.
- A stuck hand keeps holding only while pressing into its own surface still finds it (the
  prefab's cast leans into the surface the same way). Step sides now get their outward normal
  from where the floor drops away, not from the hand's motion, which trapped hands.
In the simulator, synthetic strokes went from stuck hands to about 900 units in under a minute.

### D35. Cutscenes are immersive by default
Asked for: cutscenes as 360 degrees instead of a TV. The existing "immersive" mode puts your head
in the game's cutscene camera in the stereo world (you can look anywhere, including places the
camera never shows, but can't move; the game's frustum culling is already off in VR). It's the
default now, and the letterbox bars aren't drawn in it, since they'd sit stuck to your view.
DK Arcade and Jetpac stay on the TV. First person as DK in chosen cutscenes (Cranky's lab and
the like) is not done.

### D36. Cutscenes switch between first and third person
Asked for: a button during cutscenes. In immersive cutscenes a right stick click switches between
the game's camera (360, D35) and the Kong's eyes, for that cutscene. The click doesn't reach the
game then (it was N64 L). Defaults: the shops' cutscenes (Cranky's, Funky's, Candy's, Snide's)
start in first person (`cutscene_pov`, on), everything else in third. First person faces the way
the Kong faced when the cutscene began and doesn't follow its turns or the game's camera cuts,
so nothing turns you; the eye point follows the Kong as it walks. The player model is hidden as
in play. Seen in the simulator: Funky's entrance cutscene from DK's eyes.

### D37. The head moves inside the body; arms with elbows and hands
Third headset test: "I can't even touch the ground; the arms don't look like a Gorilla Tag
player". Three causes:
- The floor came from a STAGE reading taken while the headset was still on the desk (0.30 m)
  and stayed after the runtime reset its reference space. Readings now only count for a worn
  headset (eyes 1.0-2.3 m above the floor) and are taken again after every reset.
- Head ups and downs moved the body: up lifted it off the floor (the game dropped it again,
  jitter), down was blocked by the floor, so crouching never brought the hands lower. As in
  Gorilla Tag the head now moves inside the body: only room-scale walking moves it, and the hands
  hang from the real head height.
- The arms were one beam and the hands one box. Now each arm is an upper arm and forearm with an
  elbow bent out and down (two equal bones, 0.36 m each times the arm length), and the hand is a
  palm, fingers and a thumb, turned with the controller's aim. Fur and skin colours per Kong.
  Gorilla Tag's own model isn't ours to copy; these are boxes in the same spirit.

### D38. Back to Gorilla Tag's rig: hands hold you up (replaces D34's default)
"My butt is supposed to drop if my hands are up." Right: GorillaLocomotion's body is a small
collider half a metre under the head that gravity lands; the real floor plays no part. With the
hands up you sink to gorilla height, hands on the ground hold you up and pushing down lifts you.
D34 put the game's floor at the real floor, which made the ground hard to reach at all. Now:
- Default (`match_height` off, menu "Real floor" off): the Kong's eye height over its feet at
  the Kong size scale (26 units/m: DK's eyes 0.8 m over the ground). With nothing held the game
  drops the Kong to the ground; hands on a surface hold the body (no gravity) and push it up.
- D34's real-floor mode stays as the option, with its fixes: standing height for the floor after
  a tracking reset (a Quest reset while bent down had put the floor out of reach), the ground
  5 cm above the real floor.
- Kept from D34/D37: the prefab's numbers, the release rule, step-side normals, the head inside
  the body, the arms.
- The camera base no longer subtracts the frame's head height (the body stopped following it in
  D37), so the ground doesn't bob a frame behind your head any more.
Simulator: 42 throws and ~100 units every 2 s, hops ~25 units high.

### D39. Camera blend pairs, hands into barrels, a monitor mirror
Fifth headset test ("WAY BETTER", but jitter, barrels, Cranky's exit):
- The first-person camera read the latest two game bodies at display time while the blend weight
  restarts when a workload arrives; a new body met the old weight and the view jumped ahead, then
  back, each frame while moving. The pair is now taken when the workload arrives.
- A cutscene's first / third person choice ends with the cutscene (leaving Cranky's, the choice
  carried into the barrel exit outside, seen from inside the barrel).
- Barrels (Tag, bonus, training, blast, Kong barrels) take the player only when their collision
  boxes meet while the barrel's interaction type is 1 (decomp: 7A510_collision_interact.c,
  80150_barrels.c; hidden barrels are state 0x14 with type 8). A hand touching an open barrel
  starts a jump (A) and holds the body at its centre for up to a second; the log says whether it
  took us. Not yet seen working: the simulator's test saves had no open barrel in reach.
- The desktop window can mirror the left eye (`mirror_left_eye`, on): a window-shaped crop of the
  centre, scaled with a Vulkan blit on RT64's present thread (rt64-mirror.extra.patch adds the
  hook). The copy is freed with the session.
Not done: climbing trees by gripping (tree trunks aren't in the floor-only collision the hands
use), picking up bananas and coins with the hands.

### D40. Pause, smacks, swimming
Sixth headset test: the mirror didn't show, pausing broke movement, smacking the ground
jittered, swimming didn't work.
- After a pause the hands restarted at the controllers; low ones started inside the ground,
  stayed stuck and never pulled (the body froze for half a minute). A reset now starts the hands
  at the body's centre and the first step sweeps them out to the surface; a hand let go by
  distance looks for the surface again from the head.
- A smack overshoots the ground by a whole frame of hand travel, which became a one-frame pop of
  up to 10 units. Pressing into a surface now lifts the body no faster than the throw cap per
  step (2-3 frames for a hard smack).
- The water check asked for the floor from 2000 units overhead and found roofs indoors (no
  water), so swimming was never noticed. It asks just above the head now. At the surface the
  eyes and hands are lifted over the water (the game holds the feet ~35 units under it).
- The mirror logs when it first draws into the window, or why it can't (simulator: it draws).
Open: an intermittent crash (one simulator run in five, no debugger here to see where), arm
latency (the hands are drawn at the game's 30 Hz), grip climbing, hand pickups.

### D41. Smooth hands, grip climbing, hand pickups, a crash report
"Everything":
- Arm lag: the arm and hand boxes carry interpolation tags (one per box slot), so RT64 blends
  them at the headset's rate instead of 30 Hz steps; a free hand is drawn 45 ms ahead along its
  motion. Drawing them from the live controller pose would need RT64 to move one object at display
  rate, which it can't yet.
- Grip climbing: the core takes `hand_hold` (a grip holds a hand where it is, no surface needed).
  When the game holds the Kong on something climbable (our hang detection: off the ground, not
  falling, no hand held) or did within three seconds and 40 units, a closed grip holds and the
  body follows the pulls through the movement patch. Open hands leave the game's own climbing.
  The newer decomp's state names are off from the real numbers, so climbing isn't found by state.
- Pickups: the game collects grid items within spheres around the player
  (gridSetNodeBounds = func_global_asm_806F4D70 sets the player's and clears the extras;
  gridAppendPositionNode = 806F4DC0 adds one). The patched setup adds one at each hand.
- A crash handler writes the call stack and load address to the log (addr2line on the offsets).

### D42. Start at once, the left stick only turns, shops, grips on anything the game holds
Seventh headset test:
- Start needed two presses in menus. The menu button waited up to 0.15 s for the left stick
  click to join (the VR settings chord) before sending Start. Now Start goes out at once (at
  least 0.12 s); the chord is the stick click first, or both together.
- The left stick did "actions": in arm mode its up / down sent C-up / C-down, and C-up is the
  game's first-person look. It only turns now. (C-up / C-down have no binding in arm mode.)
- Shop menus didn't work: in the shops arm mode held both sticks. The shops now leave the body
  to the game (the third-person mode), so the sticks pick moves and items.
- Grips didn't take over on trees or ropes: climbing was only noticed when the Kong hung still.
  Now it's any state off the ground other than a jump (23) or a fall (30 / 31): trees, ropes,
  vines (87 / 88), ladders, ledges.
- The mirror froze in menus: those come through RT64's flat panel, not the eyes. RT64 hands the
  host the panel right after drawing it, and the window shows it while it's on screen. Dialog
  and HUD drawn over the 3D view aren't in the mirror (they're a separate headset layer).
