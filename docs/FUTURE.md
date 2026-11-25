# Later

## Kong Tag (online)

Gorilla Tag with Kongs. Not in this pass, it is a project of its own. Rough shape if someone picks
it up:

- **What to sync.** Only the VR body: head pose, two hand poses, current Kong, a few flags (it,
  frozen). 20-30 Hz, a few hundred bytes a second per player. No game state, no actors, no RNG.
  Every client runs its own copy of the map; remote players are just puppets drawn from the
  received poses.
- **Where it plugs in.** The locomotion core already exposes body and hand positions every frame
  (`gtl_state`), so a sender reads those. Remote puppets need a draw call that can place a Kong
  model at an arbitrary pose; the hand drawing code from phase 4 is the starting point.
- **Tagging.** Distance check between a remote hand sphere and the local body sphere, decided by
  the player who got tagged (trust the victim, the classic Gorilla Tag approach, cheap and good
  enough with friends).
- **Transport.** UDP with a tiny relay or WebRTC data channels. ENet is an easy native option.
  A lobby code shared out of band is enough to start.
- **Maps.** DK Isles and Training Grounds are big and open. Load the map with no enemies and
  no objectives (a "free roam" game mode flag).
- **Problems to expect.** Map loads differ per client if anyone triggers a loading zone, so lock
  loading zones in Kong Tag. Cutscene triggers need the same treatment.

## Next up (not Kong Tag)

- **Real wall query.** Hands find walls through floor-height steps (D11). Proper walls need the
  wall triangle format (pointer table 2, `TABLE_02_MAP_WALLS`) or a matched
  `func_global_asm_8066A08C`. With it, `vr_sweep` gets one more test and room walls become climbable.
- **Hands at display rate.** Hand boxes are placed at the game's 30 Hz and interpolated by RT64.
  Drawing them from the host at display rate (latest controller pose) would remove the lag.
- **Gestures.** Plan: a small gesture layer in patches/vr_gorilla.c that turns hand motion into the
  button presses the moves already use, so the game's own move logic stays in charge:
  - Simian Slam: both hands moving down fast while airborne → Z.
  - Gun: grip near the hip → draw (Z + the Kong's gun combo), trigger → B, shots from the hand's aim
    pose (needs a projectile-origin override in the shot spawner).
  - Oranges: grip at the hip, throw → the orange combo, velocity from the hand.
  - Instruments: hand-specific motion (drum hits, strum) → the instrument combo.
  - Primate Punch / swat: fast hand into an enemy's collision → B.
  - Vines and ladders: grab with grip near the vine / ladder actor → hand over hand drives the
    existing vine/ladder states.
  Each one needs its N64 combo confirmed in the decomp's action table (0x80752C70) and a playtest.
- **Slippery surfaces.** Map the surface ids from the `[vr-loco] hand N on surface S` log to slip
  values in `vr_surface_slip` (Crystal Caves ice, slides).
- **Wrist HUD.** The HUD layer exists (`hud_mode`); anchor it to the left aim pose.
- **Meta Touch Plus profile.** Runtimes map Quest 3S controllers to the Touch profile already;
  add `/interaction_profiles/meta/touch_controller_plus` when `XR_META_touch_controller_plus` is
  offered.
- **Split into a .nrm.** All game-side VR code is in patches/vr_*.c; as a mod it could use
  `RECOMP_HOOK` instead of copying decomp functions.
