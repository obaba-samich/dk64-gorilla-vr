#!/usr/bin/env bash
# Run the build from a portable folder (run/) so it never touches an installed copy's saves.
#   tools/run.sh /path/to/dk64-us.z64   first time, copies the ROM in
#   tools/run.sh                        after that
#   DK64_VR=1 tools/run.sh              VR on (needs an OpenXR runtime, see README)
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
run="${RUN_DIR:-$repo/run}"
mkdir -p "$run"
touch "$run/portable.txt"
ln -sfn "$repo/assets" "$run/assets"
if [[ $# -ge 1 && -f "$1" ]]; then
    python3 "$repo/tools/rom_to_z64.py" "$1" "$run/DK64.z64"
    shift
fi
cd "$run"
exec "$repo/build-cmake/DK64Recompiled" --game dk64 "$@"
