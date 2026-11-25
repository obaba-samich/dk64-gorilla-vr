#!/usr/bin/env bash
# Unit tests that need neither the ROM nor a headset.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$repo/src/vr/loco/tests" -B "$repo/build-loco" -G Ninja >/dev/null
cmake --build "$repo/build-loco"
ctest --test-dir "$repo/build-loco" --output-on-failure
