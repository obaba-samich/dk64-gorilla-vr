#!/usr/bin/env bash
# Build DK64 Gorilla VR on Linux (tested on Pop!_OS 24.04).
#
#   tools/build.sh /path/to/your/dk64-us.z64     first build
#   tools/build.sh                                rebuild
#
# Needs: clang lld llvm cmake ninja-build libsdl2-dev libgtk-3-dev libvulkan-dev
#        libopenxr-dev libfreetype-dev (see README).
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

build_type="${BUILD_TYPE:-Release}"
jobs="${JOBS:-$(( $(nproc) > 2 ? $(nproc) - 2 : 1 ))}"
tools_dir="$repo/build-tools"
rom_sha1=cf806ff2603640a748fca5026ded28802f1f4a50

# N64Recomp has to match the runtime's copy, newer versions emit tables this runtime can't read.
recomp_commit="$(git -C lib/N64ModernRuntime ls-tree HEAD N64Recomp | awk '{print $3}')"

# Ubuntu's clang 18 picks the newest gcc it can find, and gcc 14 ships without libstdc++ headers
# unless libstdc++-14-dev is installed. Point it at the newest gcc that has them.
gcc_flags=""
for v in $(ls /usr/lib/gcc/x86_64-linux-gnu 2>/dev/null | sort -rn); do
    if [[ -e "/usr/lib/gcc/x86_64-linux-gnu/$v/libstdc++.so" ]]; then
        gcc_flags="--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/$v"
        break
    fi
done

git submodule update --init --recursive

if [[ $# -ge 1 ]]; then
    rom="$1"
    echo "checking $rom"
    actual="$(sha1sum "$rom" | awk '{print $1}')"
    if [[ "$actual" != "$rom_sha1" ]]; then
        # byteswapped dumps are common, fix up a copy
        tmp="$(mktemp -d)"
        python3 tools/rom_to_z64.py "$rom" "$tmp/rom.z64"
        actual="$(sha1sum "$tmp/rom.z64" | awk '{print $1}')"
        [[ "$actual" == "$rom_sha1" ]] || { echo "not the US 1.0 ROM (sha1 $actual)" >&2; exit 1; }
        rom="$tmp/rom.z64"
    fi
    work="$(mktemp -d)"
    cp "$rom" "$work/baserom.us.z64"
    (cd "$work" && python3 -I "$repo/lib/dk64_decomp/tools/generate_decompressed_rom.py" >/dev/null)
    cp "$work/baserom.us.decompressed.z64" donkeykong64.decompressed.us.z64
    rm -rf "$work"
fi

[[ -f donkeykong64.decompressed.us.z64 ]] || {
    echo "no decompressed ROM yet, run: tools/build.sh /path/to/dk64-us.z64" >&2
    exit 1
}

if [[ ! -x "$tools_dir/N64Recomp/build/N64Recomp" ]] ||
   [[ "$(git -C "$tools_dir/N64Recomp" rev-parse HEAD 2>/dev/null)" != "$recomp_commit" ]]; then
    mkdir -p "$tools_dir"
    [[ -d "$tools_dir/N64Recomp" ]] || git clone https://github.com/N64Recomp/N64Recomp.git "$tools_dir/N64Recomp"
    git -C "$tools_dir/N64Recomp" fetch -q origin
    git -C "$tools_dir/N64Recomp" checkout -q "$recomp_commit"
    git -C "$tools_dir/N64Recomp" submodule update -q --init --recursive
    cmake -S "$tools_dir/N64Recomp" -B "$tools_dir/N64Recomp/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_C_FLAGS="$gcc_flags" -DCMAKE_CXX_FLAGS="$gcc_flags" -DCMAKE_EXE_LINKER_FLAGS="$gcc_flags"
    cmake --build "$tools_dir/N64Recomp/build" --target N64RecompCLI RSPRecomp RecompModTool -j"$jobs"
fi

"$tools_dir/N64Recomp/build/N64Recomp" us.toml
"$tools_dir/N64Recomp/build/RSPRecomp" n_aspMain.toml

if [[ -x tools/vr/prepare.sh ]]; then
    tools/vr/prepare.sh
fi

cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="$gcc_flags" -DCMAKE_CXX_FLAGS="$gcc_flags" \
    -DCMAKE_EXE_LINKER_FLAGS="$gcc_flags" -DCMAKE_SHARED_LINKER_FLAGS="$gcc_flags"
cmake --build build-cmake --target DK64Recompiled -j"$jobs"

echo
echo "built build-cmake/DK64Recompiled"
echo "run it with: tools/run.sh"
