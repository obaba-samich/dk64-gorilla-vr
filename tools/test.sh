#!/usr/bin/env bash
# Unit tests that need neither the ROM nor a headset.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$repo/src/vr/loco/tests" -B "$repo/build-loco" -G Ninja >/dev/null
cmake --build "$repo/build-loco"
ctest --test-dir "$repo/build-loco" --output-on-failure

# VR host tests (pure bridge code, no headset)
gcc_flags=""
for v in $(ls /usr/lib/gcc/x86_64-linux-gnu 2>/dev/null | sort -rn); do
    if [[ -e "/usr/lib/gcc/x86_64-linux-gnu/$v/libstdc++.so" ]]; then
        gcc_flags="--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/$v"
        break
    fi
done
cmake -S "$repo/src/vr/tests" -B "$repo/build-vrtest" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="$gcc_flags" -DCMAKE_CXX_FLAGS="$gcc_flags" -DCMAKE_EXE_LINKER_FLAGS="$gcc_flags" >/dev/null
cmake --build "$repo/build-vrtest"
ctest --test-dir "$repo/build-vrtest" --output-on-failure
