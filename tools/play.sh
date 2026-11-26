#!/usr/bin/env bash
# One command for a headset session: build, start WiVRn, wait for Enter, start the game in VR.
# Ctrl+C (or quitting the game) closes everything it started.
#   tools/play.sh                 build, WiVRn, game
#   tools/play.sh --no-build      skip the build
#   tools/play.sh /path/rom.z64   first run: the ROM is copied into run/ (passed to run.sh)
# Linked as `dk64vr` in ~/.local/bin by tools/install-play.sh, so it runs from any folder.
set -uo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
wivrn=io.github.wivrn.wivrn
game_pid=""
cleaned=0

cleanup() {
    [[ $cleaned == 1 ]] && return
    cleaned=1
    echo
    echo "[play] closing everything..."
    if [[ -n "$game_pid" ]] && kill -0 "$game_pid" 2>/dev/null; then
        kill -TERM "$game_pid" 2>/dev/null
        for _ in 1 2 3 4 5 6; do kill -0 "$game_pid" 2>/dev/null || break; sleep 0.5; done
        kill -KILL "$game_pid" 2>/dev/null
    fi
    pkill -TERM -x DK64Recompiled 2>/dev/null
    flatpak kill "$wivrn" 2>/dev/null
    echo "[play] done."
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

build=1
args=()
for a in "$@"; do
    if [[ "$a" == "--no-build" ]]; then build=0; else args+=("$a"); fi
done

if ! flatpak info "$wivrn" >/dev/null 2>&1; then
    echo "[play] WiVRn isn't installed: flatpak install --user flathub $wivrn" >&2
    exit 1
fi

if [[ $build == 1 ]]; then
    echo "[play] building..."
    "$repo/tools/build.sh" || { echo "[play] the build failed, see above" >&2; exit 1; }
fi

echo "[play] starting WiVRn (its window shows the pairing PIN the first time)..."
flatpak run "$wivrn" >/dev/null 2>&1 &
disown

echo
echo "[play] Put the headset on, open WiVRn there and connect to this PC."
read -r -p "[play] Press Enter to start the game (Ctrl+C closes everything)... " _

# point the game at WiVRn's runtime directly, so it doesn't depend on the active-runtime file
json="$(flatpak info --show-location "$wivrn" 2>/dev/null)/files/share/openxr/1/openxr_wivrn.json"
if [[ -f "$json" ]]; then
    export XR_RUNTIME_JSON="$json"
fi

echo "[play] starting the game (output also in run/logs/last-run.log)..."
"$repo/tools/run.sh" "${args[@]}" --vr &
game_pid=$!
wait "$game_pid"
echo "[play] the game closed."
