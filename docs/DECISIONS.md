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
