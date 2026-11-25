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
