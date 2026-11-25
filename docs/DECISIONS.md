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
